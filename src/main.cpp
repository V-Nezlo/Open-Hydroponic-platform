/*
Автор - V-Nezlo
email: vlladimirka@gmail.com
Дата создания проекта - 10.12.2021

Фичи, которые нужно реализовать:
- Измерять уровень воды будем с помощью тензодатчика и измерения массы емкости с водой,
поплавковый уровень это не серьезно

УСТАРЕЛО
(Концевик на бак, без него во время замены жидности насос может включиться насухую,
ИЛИ ограничиться остановкой алгоритма по поплавковому уровню внутри бака)

Фичи, которые возможно будут реализованы:
- Добавление наблюдателя для контроля над выполнением основной программы
- Добавление датчиков протечки на узлы, которые в теории могут потечь
- Постоянное сохранение всех параметров системы в FRAM память
- Перенести работу лампы так же в unixtime

*/

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <EncButton.h>
#include <Adafruit_GFX.h>
#include <avr/eeprom.h>
#include <RTClib.h>
#include "TimeContainer.hpp"

enum class DisplayModes : uint8_t {
	TIME,
	PH_PPM,
	PUMP_TIMINGS,
	LAMP_TIMINGS,
	STATUS,
	SET_CUR_TIME,
	SET_LAMPON_TIME,
	SET_LAMPOFF_TIME,
	SET_PUMP_TIME,
	SET_SWING_PERIOD,
	SET_WORKMODE,
	ERROR_NOFLOATLEV,
	SET_MAXFLOODTIME
} displayMode;

enum class Periphs {
	PUMP,
	LAMP,
	REDLED,
	BLUELED,
	GREENLED,
	ZUMMER
};

enum class ErrorTypes {
	WARNING, // Предупреждение
	ERROR, // Ошибка, нужно произвести какие то действия чтобы продолжить
	CRITICAL // Критическая ошибка, выключение
};

enum class HydroTypes {
	NORMAL,
	SWING,
} hydroType;;

struct TimeContainerMinimal {
	uint8_t hours;
	uint8_t minutes;
};

struct EepromData {
	uint8_t pumpOnPeriod;
	uint8_t pumpOffPeriod;
	TimeContainerMinimal lampOnTime;
	TimeContainerMinimal lampOffTime;
	uint8_t swingOffPeriod;
	HydroTypes hydroType;
	uint16_t maxTimeForFullFlood;
};

struct Statistics {
	uint32_t successed; // Полных циклов (пока не используется)
	uint32_t errors; // Ошибок
};

static constexpr char kSWVersion[]{"0.7"}; // Текущая версия прошивки
static constexpr unsigned long kDisplayUpdateTime{300}; // Время обновления информации на экране
static constexpr unsigned long kRTCReadTime{1000}; // Период опроса RTC
static constexpr uint8_t kMaxPumpPeriod{60}; // Максимальная длительность периода залива-отлива в минутах
static constexpr uint8_t kMaxSwingPeriod{30}; // Максимальный период раскачивания в секундах
static constexpr uint16_t kMaxTimeForFlood{300}; // Максимально настраиваемое время заполнения камеры в секундах
static constexpr uint16_t kErrorBlinkingPeriod{500}; // Миллисекунды
static constexpr uint8_t kErrorCleanPeriod{1}; // Время, по прошествии которого ошибка сбросится сама в минутах 
static constexpr uint8_t kRedLedPin{5};
static constexpr uint8_t kGreenLedPin{7};
static constexpr uint8_t kBlueLedPin{6};
static constexpr uint8_t kPumpPin{12};
static constexpr uint8_t kLampPin{13};
static constexpr uint8_t kFloatLevelPin{8};
static constexpr uint8_t kZummerPin{9};

static constexpr uint8_t kEncKeyPin{4};
static constexpr uint8_t kEncS2Pin{2};
static constexpr uint8_t kEncS1Pin{3};

Adafruit_SSD1306 display(7);
EncButton<EB_CALLBACK, kEncS1Pin, kEncS2Pin ,kEncKeyPin> encoder(INPUT_PULLUP);

RTC_DS3231 rtc;
uint32_t pumpNextSwitchTime{0};
uint32_t pumpNextCheckTime{0};
uint32_t pumpNextSwingTime{0};

TimeContainer lampOnTime{0,0};
TimeContainer lampOffTime{0,0};

uint8_t swingOffPeriod{0}; // Время состояния "качелей" выключено в секундах
uint8_t pumpOnPeriod{0};
uint8_t pumpOffPeriod{0};
uint16_t maxTimeForFullFlood{0};
uint32_t nextDisplayTime{0}; // Время следующего обновления экрана
uint32_t nextRTCReadTime{0}; // Время следующего чтения RTC
uint32_t nextErrorCleanTime{0}; // Время следующего сброса ошибки
uint32_t nextErrorBlinkTime{0}; // Не unixTime а millis
uint32_t lastErrorTime{0}; // Время последней ошибки

uint8_t currentPH{0};
uint16_t currentPPM{0};

bool swingState{false};
bool pumpState{false};
bool lampState{false};
bool modeConf{false};
bool errorState{false};
bool errorStatePos{false};

// Флаги для разных проверок
bool pumpCheckNeeded{false};
Statistics statistics{0,0};
//

void eepromWrite();
void eepromRead();

void pinInit()
{
	pinMode(kRedLedPin, OUTPUT);
	pinMode(kBlueLedPin, OUTPUT);
	pinMode(kGreenLedPin, OUTPUT);
	pinMode(kPumpPin, OUTPUT);
	pinMode(kLampPin, OUTPUT);
	pinMode(kZummerPin, OUTPUT);
	pinMode(kFloatLevelPin, INPUT_PULLUP);
	
	// Пины для энкодера инициализируются внутри библиотеки Гайвера, кроме кнопки энкодера
	pinMode(kEncKeyPin, INPUT_PULLUP);
}

void oledInit()
{
	display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
	display.clearDisplay();
	display.setTextSize(1);
	display.setRotation(2);
	display.setTextColor(WHITE);
	display.setCursor(0, 0);
}

void encoderInit()
{
	encoder.setHoldTimeout(1500);

	encoder.attach(RIGHT_HANDLER, [](){
		// Лямда с обработчиком движения энкодера вправо
		DateTime now = rtc.now();

		if (!modeConf) {
			switch(displayMode){
				case DisplayModes::TIME:
					displayMode = DisplayModes::PH_PPM;
					break;
				case DisplayModes::PH_PPM:
					displayMode = DisplayModes::PUMP_TIMINGS;
					break;
				case DisplayModes::PUMP_TIMINGS:
					displayMode = DisplayModes::LAMP_TIMINGS;
					break;
				case DisplayModes::LAMP_TIMINGS:
					displayMode = DisplayModes::STATUS;
					break;
				case DisplayModes::STATUS:
					displayMode = DisplayModes::TIME;
					break;
				default:
					break;
			}
		} else {
			switch (displayMode) {
				case DisplayModes::SET_CUR_TIME:
					if (now.hour() < 23) {
						rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour() + 1, now.minute(), now.second()));
					} else {
						rtc.adjust(DateTime(now.year(), now.month(), now.day(), 0, now.minute(), now.second()));
					}
					break;
				case DisplayModes::SET_LAMPON_TIME:
					if (lampOnTime.hour() <= 12) {
						lampOnTime.setTime(lampOnTime.hour() + 1, lampOnTime.minute(), lampOnTime.seconds());
					} else {
						lampOnTime.setTime(0, lampOnTime.minute(), lampOnTime.seconds());
					}
					break;
				case DisplayModes::SET_LAMPOFF_TIME:
					if (lampOffTime.hour() < 23) {
						lampOffTime.setTime(lampOffTime.hour() + 1, lampOffTime.minute(), lampOffTime.seconds());
					} else {
						lampOffTime.setTime(12, lampOffTime.minute(), lampOffTime.seconds());
					}
					break;
				case DisplayModes::SET_PUMP_TIME:
					if (pumpOnPeriod < kMaxPumpPeriod) {
						++pumpOnPeriod;
					} else {
						pumpOnPeriod = 1;
					}
					break;
				case DisplayModes::SET_SWING_PERIOD:
					if (swingOffPeriod < kMaxSwingPeriod) {
						++swingOffPeriod;
					} else {
						swingOffPeriod = 1;
					}
					break;
				case DisplayModes::SET_WORKMODE:
					if (hydroType == HydroTypes::NORMAL) {
						hydroType = HydroTypes::SWING;
					} else {
						hydroType = HydroTypes::NORMAL;
					}
					break;
				case DisplayModes::SET_MAXFLOODTIME:
					if (maxTimeForFullFlood < kMaxTimeForFlood) {
						++maxTimeForFullFlood;
					} else {
						maxTimeForFullFlood = 10;
					}
					break;
				default:
					break;	
			}
		}
	});
	encoder.attach(LEFT_HANDLER, [](){
		// Лямда с обработчиком движения энкодера влево
		DateTime now = rtc.now();

		if (!modeConf) {
			switch(displayMode){
				case DisplayModes::TIME:
					displayMode = DisplayModes::STATUS;
					break;
				case DisplayModes::PH_PPM:
					displayMode = DisplayModes::TIME;
					break;
				case DisplayModes::PUMP_TIMINGS:
					displayMode = DisplayModes::PH_PPM;
					break;
				case DisplayModes::LAMP_TIMINGS:
					displayMode = DisplayModes::PUMP_TIMINGS;
					break;
				case DisplayModes::STATUS:
					displayMode = DisplayModes::LAMP_TIMINGS;
					break;
				default:
					break;
			}
		} else {
			switch (displayMode) {
				case DisplayModes::SET_CUR_TIME:
					if (now.minute() < 59) {
						rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), now.minute() + 1, now.second()));
					} else {
						rtc.adjust(DateTime(now.year(), now.month(), now.day(), now.hour(), 0, now.second()));
					}
					break;
				case DisplayModes::SET_LAMPON_TIME:
					if (lampOnTime.minute() < 59) {
						lampOnTime.setTime(lampOnTime.hour(), lampOnTime.minute() + 1, lampOnTime.seconds());
					} else {
						lampOnTime.setTime(lampOnTime.hour(), 0, lampOnTime.seconds());
					}
					break;
				case DisplayModes::SET_LAMPOFF_TIME:
					if (lampOffTime.minute() < 59) {
						lampOffTime.setTime(lampOffTime.hour(), lampOffTime.minute() + 1, lampOffTime.seconds());
					} else {
						lampOffTime.setTime(lampOffTime.hour(), 0, lampOffTime.seconds());
					}
					break;
				case DisplayModes::SET_PUMP_TIME:
					if (pumpOffPeriod < kMaxPumpPeriod) {
						++pumpOffPeriod;
					} else {
						pumpOffPeriod = 1;
					}
					break;
				case DisplayModes::SET_SWING_PERIOD:
					if (swingOffPeriod > 1) {
						--swingOffPeriod;
					} else {
						swingOffPeriod = kMaxSwingPeriod;
					}
					break;
				case DisplayModes::SET_WORKMODE:
					if (hydroType == HydroTypes::NORMAL) {
						hydroType = HydroTypes::SWING;
					} else {
						hydroType = HydroTypes::NORMAL;
					}
					break;
				case DisplayModes::SET_MAXFLOODTIME:
					if (maxTimeForFullFlood > 1) {
						--maxTimeForFullFlood;
					} else {
						maxTimeForFullFlood = kMaxTimeForFlood;
					}
					break;
				default:
					break;	
			}
		}
	});
	encoder.attach(PRESS_HANDLER, [](){
		// Лямбда с обработчиком коротких нажатий энкодера

		if (modeConf) {
			switch (displayMode) {
				case DisplayModes::SET_CUR_TIME:
					displayMode = DisplayModes::SET_LAMPON_TIME;
					break;
				case DisplayModes::SET_LAMPON_TIME:
					displayMode = DisplayModes::SET_LAMPOFF_TIME;
					break;
				case DisplayModes::SET_LAMPOFF_TIME:
					displayMode = DisplayModes::SET_PUMP_TIME;
					break;
				case DisplayModes::SET_PUMP_TIME:
					displayMode = DisplayModes::SET_MAXFLOODTIME;
					break;
				case DisplayModes::SET_MAXFLOODTIME:
					displayMode = DisplayModes::SET_WORKMODE;
					break;
				case DisplayModes::SET_WORKMODE:
					if (hydroType == HydroTypes::SWING) {
						displayMode = DisplayModes::SET_SWING_PERIOD;
					} else {
						displayMode = DisplayModes::SET_CUR_TIME;
					}

					break;
				case DisplayModes::SET_SWING_PERIOD:
					displayMode = DisplayModes::SET_CUR_TIME;
					break;
				default:
					break;
			}
		}

		if (errorState) {
			errorState = false; // Сбросим флаг ошибки отсюда (временно)
		}
	});

	encoder.setHoldTimeout(1000);

	encoder.attach(HOLD_HANDLER, [](){
		// Лямбда с обработчиком длинных нажатий энкодера

		if (modeConf) {
			modeConf = false;
			eepromWrite();
			displayMode = DisplayModes::TIME;
		} else {
			modeConf = true;
			displayMode = DisplayModes::SET_CUR_TIME;
		}
	});
}

void switchPeriph(Periphs aPeriph, bool aMode)
{
	switch(aPeriph) {
		case Periphs::PUMP:
			digitalWrite(kPumpPin, aMode);
			break;
		case Periphs::LAMP:
			digitalWrite(kLampPin, aMode);
			lampState = aMode;
			break;
		case Periphs::REDLED:
			digitalWrite(kRedLedPin, aMode);
			break;
		case Periphs::BLUELED:
			digitalWrite(kBlueLedPin, aMode);
			break;
		case Periphs::GREENLED:
			digitalWrite(kGreenLedPin, aMode);
			break;
		case Periphs::ZUMMER:
			digitalWrite(kZummerPin, aMode);
			break;
	}
}

void handleError(ErrorTypes aType)
{
	DateTime now = rtc.now();
	uint32_t currentUnixTime{now.unixtime()};

	errorState = true; // Поставим флаг ошибки
	nextErrorCleanTime = currentUnixTime + (60 * kErrorCleanPeriod);
	lastErrorTime = currentUnixTime;

	switch (aType) {
		case ErrorTypes::CRITICAL: 
			switchPeriph(Periphs::REDLED, true);
			switchPeriph(Periphs::GREENLED, false);
			switchPeriph(Periphs::PUMP, false);
			switchPeriph(Periphs::LAMP, false);
			while (true) {} // Пока что это критическая ошибка и ее возникновение говорит о потопе, используется только в NORMAL режиме
			
		case ErrorTypes::ERROR: // Ошибка, требующая сброса
			++statistics.errors; // Инкремент счетчика ошибок
			break;
		case ErrorTypes::WARNING: // Предупреждение
			break;
	}
}

void checkTime()
{
	DateTime now = rtc.now();
	TimeContainer currentTime{now.hour(), now.minute(), now.second()}; // Остается для работы лампы по часам
	uint32_t currentUnixTime{now.unixtime()};                          // Добавляется для правильного подсчета интервалов работы насоса
	uint32_t currentMillisTime{millis()};                              // А тут можно получить миллисекунды от мк

	switch (hydroType) {
		case HydroTypes::NORMAL :{
			// Нормальный режим - просто переключаем насос по интервалам
			// Проверим тайминги для насоса
			if (currentUnixTime > pumpNextSwitchTime) {
			// Если пришло время переключения - переключаем

				if (!pumpState) {
					pumpNextSwitchTime += (60 * pumpOnPeriod);
					switchPeriph(Periphs::PUMP, true);
					switchPeriph(Periphs::BLUELED, true);
					Serial.println("pump on!");
					pumpState = true;

					// Включаем таймер для проверки статуса поплавкого уровня внутри камеры
					pumpNextCheckTime = currentUnixTime + maxTimeForFullFlood; 
					pumpCheckNeeded = true;
				} else {
					pumpNextSwitchTime += (60 * pumpOffPeriod);
					switchPeriph(Periphs::PUMP, false);
					switchPeriph(Periphs::BLUELED, false);
					Serial.println("pump off!");
					pumpState = false;
				}
			}

			if ((currentUnixTime > pumpNextCheckTime) && pumpCheckNeeded) {
				if (digitalRead(kFloatLevelPin)) {
					pumpCheckNeeded = false; // Основная камера затоплена за требуемое время, все в порядке
				} else {
					handleError(ErrorTypes::CRITICAL); // Что-то пошло не так
				}
			}
			break;
		} // HydroTypes::Normal
		case HydroTypes::SWING :{
			// Видоизмененный нормальный режим. В период затопления насос активен не все время,
			// он выключается по срабатыванию поплавкового уровня в камере и включается по таймауту
			if (currentUnixTime > pumpNextSwitchTime) {
				// Переключаем режимы так же как в нормальном но не трогаем сам насос
				if (!pumpState) {
					pumpNextSwitchTime += (60 * pumpOnPeriod);
					Serial.println("pump swing enable!");
					pumpState = true;
					swingState = false;  //Начинаем с положения вкл
					switchPeriph(Periphs::BLUELED, true);
				} else {
					pumpNextSwitchTime += (60 * pumpOffPeriod);
					pumpState = false;
					Serial.println("pump off!");
					switchPeriph(Periphs::BLUELED, false);
				}
			}

			if (!pumpState) {
				switchPeriph(Periphs::PUMP, false); // Если насос не включен - то определенно он должен быть выключен
				pumpCheckNeeded = false; // Этот флаг может не сброситься сам после окончания цикла, сбросим вручную
			} else {
				// Если насос включен - начинаем "качели"
				if (!swingState && currentUnixTime > pumpNextSwingTime) { // Если доп флаг насоса выключен и время для переключения пришло
					switchPeriph(Periphs::PUMP, true); // включаем насос
					pumpNextCheckTime = currentUnixTime + maxTimeForFullFlood; // Добавляем проверку на возможность затопления
					pumpCheckNeeded = true; //активируем проверку
					swingState = true;
					Serial.println("swing on!");
					
				} else if (digitalRead(kFloatLevelPin) && swingState == true) {
					// Если концевик сработал
					switchPeriph(Periphs::PUMP, false); // Выключим насос
					pumpNextSwingTime = currentUnixTime + swingOffPeriod; // Заведем таймер на интервал ожидания
					pumpCheckNeeded = false;
					swingState = false;
					Serial.println("swing off!");
				} else if (pumpCheckNeeded && currentUnixTime > pumpNextCheckTime) {
					// Если оно долго не сбрасывалось - значит что-то пошло не так, например застрял поплавковый уровень
					switchPeriph(Periphs::PUMP, false); // Выключим насос
					pumpNextSwingTime = currentUnixTime + swingOffPeriod; // Заведем таймер на интервал ожидания
					swingState = false;
					pumpCheckNeeded = false;
					handleError(ErrorTypes::ERROR); // Поставим ошибку
					Serial.println("swing off from timer, float level faillure");
				} 
			}
			break;
		} // HydroTypes::Swing
	}

	// Проверим тайминги для лампы
	if (currentTime < lampOnTime || currentTime > lampOffTime) {
		switchPeriph(Periphs::LAMP, false);
	} else {
		switchPeriph(Periphs::LAMP, true);
	}

	// Проверим ошибки, но по таймеру, чтобы не мучать периферию
	if (currentMillisTime > nextErrorBlinkTime + kErrorBlinkingPeriod) {

		if (errorState) {
			switchPeriph(Periphs::GREENLED, false); // Снимем зеленый светодиод, у нас ошибка

			if (errorStatePos) {
				switchPeriph(Periphs::REDLED, true);
				switchPeriph(Periphs::ZUMMER, true);
			} else {
				switchPeriph(Periphs::REDLED, false);
				switchPeriph(Periphs::ZUMMER, false);
			}

			errorStatePos = !errorStatePos;

		} else {
			switchPeriph(Periphs::REDLED, false);
			switchPeriph(Periphs::ZUMMER, false);
			switchPeriph(Periphs::GREENLED, true);
		}
	}

	// Сбросим ошибку если пришло время ее сбросить
	if (errorState && (currentUnixTime > nextErrorCleanTime)) {
		errorState = false;
	}
}

void eepromRead()
{
	EepromData data;
	eeprom_read_block(static_cast<void*>(&data), 0, sizeof(data));
	pumpOnPeriod = data.pumpOnPeriod;
	pumpOffPeriod = data.pumpOffPeriod;
	lampOnTime.setTime(data.lampOnTime.hours, data.lampOnTime.minutes, 0);
	lampOffTime.setTime(data.lampOffTime.hours, data.lampOffTime.minutes, 0);
	swingOffPeriod = data.swingOffPeriod;
	hydroType = data.hydroType;
	maxTimeForFullFlood = data.maxTimeForFullFlood;
}

void eepromWrite()
{
	EepromData data{pumpOnPeriod, pumpOffPeriod, {lampOnTime.hour(), lampOnTime.minute()}, {lampOffTime.hour(), lampOffTime.minute()}, 
		swingOffPeriod, hydroType, maxTimeForFullFlood};
	eeprom_update_block(static_cast<void*>(&data), 0, sizeof(data));
}

String getHydroTypeName()
{
	switch (hydroType) {
		case HydroTypes::NORMAL:
			return "Normal";
			break;
		case HydroTypes::SWING:
			return "Normal-swing";
			break;
	}
	return "Unknown";
}

void displayProcedure()
{
	String str1;
	String str2;
	DateTime now{rtc.now()};

	switch(displayMode){
		case DisplayModes::TIME:
			str1 = "Current time";
			str2 += now.hour() / 10;
			str2 += now.hour() % 10;
			str2 += ":";
			str2 += now.minute() / 10;
			str2 += now.minute() % 10;

			display.clearDisplay();
			display.setCursor(0,0);
			display.print(str1);
			display.setCursor(60, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::PH_PPM:
			str1 = "PH = ";
			str1 += currentPH;
			str2 = "PPM = ";
			str2 += currentPPM;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::PUMP_TIMINGS:
			str1 = "Flood = ";
			str1 += pumpOnPeriod;
			str2 = "Drain = ";
			str2 += pumpOffPeriod;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::LAMP_TIMINGS:
			str1 = "Lamp on:";
			str1 += lampOnTime.hour() / 10;
			str1 += lampOnTime.hour() % 10;
			str1 += ":";
			str1 += lampOnTime.minute() / 10;
			str1 += lampOffTime.minute() % 10;
			str2 = "Lamp off:";
			str2 += lampOffTime.hour() / 10;
			str2 += lampOffTime.hour() % 10;
			str2 += ":";
			str2 += lampOffTime.minute() / 10;
			str2 += lampOffTime.minute() % 10;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::STATUS:
			str1 = "Ver: ";
			str1 += kSWVersion;
			str2 = "Errors: ";
			str2 += statistics.errors;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_CUR_TIME:
			str1 = "Set Cur time";
			str2 += now.hour() / 10;
			str2 += now.hour() % 10;
			str2 += ":";
			str2 += now.minute() / 10;
			str2 += now.minute() % 10;

			display.clearDisplay();
			display.setCursor(0,0);
			display.print(str1);
			display.setCursor(60, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_LAMPON_TIME:
			str1 = "Set LampOn time";
			str2 += lampOnTime.hour() / 10;
			str2 += lampOnTime.hour() % 10;
			str2 += ":";
			str2 += lampOnTime.minute() / 10;
			str2 += lampOnTime.minute() % 10;

			display.clearDisplay();
			display.setCursor(0,0);
			display.print(str1);
			display.setCursor(60, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_LAMPOFF_TIME:
			str1 = "Set LampOff time";
			str2 += lampOffTime.hour() / 10;
			str2 += lampOffTime.hour() % 10;
			str2 += ":";
			str2 += lampOffTime.minute() / 10;
			str2 += lampOffTime.minute() % 10;

			display.clearDisplay();
			display.setCursor(0,0);
			display.print(str1);
			display.setCursor(60, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_PUMP_TIME:
			str1 = "SetFlood = ";
			str1 += pumpOnPeriod;
			str2 = "SetDrain = ";
			str2 += pumpOffPeriod;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_SWING_PERIOD:
			str1 = "Swing period";
			str2 = swingOffPeriod;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_WORKMODE:
			str1 = "Work Mode is:";
			str2 = getHydroTypeName();
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::ERROR_NOFLOATLEV:
			str1 = "Float level error";
			str2 = "Plug float level";
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		case DisplayModes::SET_MAXFLOODTIME:
			str1 = "Max flood time";
			str2 = maxTimeForFullFlood;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
			display.display();
			break;
		default:
			break;
	}		
}

void firstInit()
{
	rtc.adjust(DateTime(__DATE__, __TIME__)); // Заберем время из системы во время компиляции
	lampOnTime.setTime(7, 0);
	lampOffTime.setTime(23, 30);
	pumpOnPeriod = 15;
	pumpOffPeriod = 10;
	swingOffPeriod = 10;
	hydroType = HydroTypes::SWING;
}

void setup()
{
	Serial.begin(115200);
	rtc.begin();
	pinInit();
	eepromRead(); // Сначала вспомнили из еепром

	if (!digitalRead(kEncKeyPin)) { // потом если надо залили сверху
		firstInit();
	}

	encoderInit();
	oledInit();
	switchPeriph(Periphs::GREENLED, true);

	if (digitalRead(kFloatLevelPin)) { // Проверяем на старте есть ли поплавковый уровень в системе
		displayMode = DisplayModes::ERROR_NOFLOATLEV; // Если нет - ошибка, без него работать нельзя, ошибка несбрасываемая
		handleError(ErrorTypes::ERROR);
	} else {
		displayMode = DisplayModes::TIME; // Иначе включаемся
	}

	DateTime now{rtc.now()};
	uint32_t currentUnixTime{now.unixtime()};

	pumpNextSwitchTime = currentUnixTime + (60 * pumpOffPeriod); // Начинаем цикл с положения выкл
}

void loop()
{
	encoder.tick();

	uint32_t currentTime = millis();
	if (currentTime - nextDisplayTime >= kDisplayUpdateTime) {
		nextDisplayTime = currentTime;
		displayProcedure();
	}

	if (currentTime - nextRTCReadTime >= kRTCReadTime) {
		nextRTCReadTime = currentTime;
		checkTime();
	}
}
