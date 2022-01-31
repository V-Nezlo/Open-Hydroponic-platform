/*
Автор - V-Nezlo
email: vlladimirka@gmail.com
Дата создания проекта - 10.12.2021

Фичи, которые нужно реализовать:
 - Концевик на бак, без него во время замены жидности насос может включиться насухую,
ИЛИ ограничиться остановкой алгоритма по поплавковому уровню внутри бака

Фичи, которые возможно будут реализованы:
- Добавление второго поплавкого уровня для бака
- Добавление наблюдателя для контроля над выполнением основной программы
- Добавление датчиков протечки на узлы, которые в теории могут потечь
- Добавление зуммера для дополнительной индикации проблем в системе
- Постоянное сохранение всех параметров системы в FRAM память
- Перенести работу лампы так же в unixtime
- Первая инициализация через зажатие кнопки при включении

и т.д.
*/

#include <Arduino.h>
#include <Adafruit_SSD1306.h>
#include <EncButton.h>
#include <Adafruit_GFX.h>
#include <EEPROM.h>
#include <RTClib.h>
#include "TimeContainer.hpp"

enum class DisplayModes : uint8_t {
	TIME               = 1,
	PH_PPM             = 2,
	PUMP_TIMINGS       = 3,
	LAMP_TIMINGS       = 4,
	STATUS             = 5,
	SET_CUR_TIME       = 6,
	SET_LAMPON_TIME    = 7,
	SET_LAMPOFF_TIME   = 8,
	SET_PUMP_TIME      = 9
} displayMode;

enum class Periphs {
	PUMP,
	LAMP,
	REDLED,
	BLUELED,
	GREENLED
};

enum class ErrorTypes {
	LOW_WATERLEVEL,
	LEAK,
	POWEROFF
};

enum class HydroTypes {
	NORMAL,
	SWING,
} hydroType;;

struct TimeContainerMinimal {
	uint8_t hours;
	uint8_t minutes;
};

struct EepromData{
	uint8_t pumpOnPeriod;
	uint8_t pumpOffPeriod;
	TimeContainerMinimal lampOnTime;
	TimeContainerMinimal lampOffTime;
};

static constexpr char kSWVersion[]{"0.5"}; // Текущая версия прошивки
static constexpr unsigned long kDisplayUpdateTime{300}; // Время обновления информации на экране
static constexpr unsigned long kRTCReadTime{1000}; // Период опроса RTC
static constexpr uint8_t kMaxPumpPeriod{60}; // Максимальная длительность периода залива-отлива в минутах
static constexpr uint8_t kMaxTimeForFullFlood{60}; // Максимальная длительность работы насоса для полного затопления камеры в секундах
static constexpr uint8_t kSwingOffPeriod{10}; // Время состояния "качелей" выключено в секундах

static constexpr uint8_t kRedLedPin{5};
static constexpr uint8_t kGreenLedPin{7};
static constexpr uint8_t kBlueLedPin{6};
static constexpr uint8_t kPumpPin{12};
static constexpr uint8_t kLampPin{13};
static constexpr uint8_t kFloatLevelPin{8};

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

uint8_t pumpOnPeriod{0};
uint8_t pumpOffPeriod{0};
uint64_t nextDisplayTime{0};
uint64_t nextRTCReadTime{0};

uint8_t currentPH{0};
uint16_t currentPPM{0};

bool swingState{false};
bool pumpState{false};
bool lampState{false};
bool modeConf{false};

// Флаги для разных проверок
bool pumpCheckNeeded{false};
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
	pinMode(kFloatLevelPin, INPUT_PULLUP);
	
	// Пины для энкодера инициализируются внутри библиотеки Гайвера
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
						pumpOnPeriod = 0;
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
						pumpOffPeriod = 0;
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
					displayMode = DisplayModes::SET_CUR_TIME;
					break;
				default:
				break;
			}
		}
	});
	encoder.attach(HOLD_HANDLER, [](){
		// Лямбда с обработчиком длинных нажатий энкодера
		encoder.setHoldTimeout(1500);

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
		switchPeriph(Periphs::BLUELED, aMode); // Рекурсивно
		//pumpState = aMode; Ушло в обработчик времени
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
	}
}

void handleError(ErrorTypes aType)
{
	switch (aType) {
		case ErrorTypes::LOW_WATERLEVEL: // Если емкость не заполнена за нужное время - рубим все, включаем красный светодиод и уходим в вечное ожидание
			switchPeriph(Periphs::REDLED, true);
			switchPeriph(Periphs::PUMP, false);
			switchPeriph(Periphs::LAMP, false);
			while(true) {} //
		case ErrorTypes::LEAK: // Если будет датчик протечки - он будет обрабатываться тут
		break;
		case ErrorTypes::POWEROFF: // Если будет датчик наличия напряжения - он будет обрабатываться здесь
		break;
	}
}

void checkTime()
{
	DateTime now = rtc.now();
	TimeContainer currentTime{now.hour(), now.minute(), now.second()}; // Остается для работы лампы по часам
	uint32_t currentUnixTime{now.unixtime()};                          // Добавляется для правильного подсчета интервалов работы насоса

	switch (hydroType) {
		case HydroTypes::NORMAL :{
			// Нормальный режим - просто переключаем насос по интервалам
			// Проверим тайминги для насоса
			if (currentUnixTime > pumpNextSwitchTime) {
			// Если пришло время переключения - переключаем

				if (!pumpState) {
					pumpNextSwitchTime += (60 * pumpOnPeriod);
					switchPeriph(Periphs::PUMP, true);
					Serial.println("pump on!");
					pumpState = true;

					// Включаем таймер для проверки статуса поплавкого уровня внутри камеры
					pumpNextCheckTime = currentUnixTime + kMaxTimeForFullFlood; 
					pumpCheckNeeded = true;
				} else {
					pumpNextSwitchTime += (60 * pumpOffPeriod);
					switchPeriph(Periphs::PUMP, false);
					Serial.println("pump off!");
					pumpState = false;
				}
			}

			if ((currentUnixTime > pumpNextCheckTime) && pumpCheckNeeded) {
				if (digitalRead(kFloatLevelPin)) {
					pumpCheckNeeded = false; // Основная камера затоплена за требуемое время, все в порядке
				} else {
					handleError(ErrorTypes::LOW_WATERLEVEL); // Что-то пошло не так
				}
			}

		} // HydroTypes::Normal
		case HydroTypes::SWING :{
			// Видоизмененный нормальный режим. В период затопления насос активен не все время,
			// он выключается по срабатыванию поплавкового уровня в камере и включается по таймауту
			if (currentUnixTime > pumpNextSwitchTime) {
				// Переключаем режимы так же как в нормальном но не трогаем сам насос
				if (!pumpState) {
					pumpNextSwitchTime += (60 * pumpOnPeriod);
					pumpState = true;
				} else {
					pumpNextSwitchTime += (60 * pumpOffPeriod);
					pumpState = false;
				}
			}

			if (!pumpState) {
				switchPeriph(Periphs::PUMP, false); // Если насос не включен - то определенно он должен быть выключен
			} else {
				// Если насос включен - начинаем "качели"
				if (!swingState && currentUnixTime > pumpNextSwingTime) { // Если доп флаг насоса выключен и время для переключения пришло
					switchPeriph(Periphs::PUMP, true); // включаем насос
					pumpNextCheckTime = currentUnixTime + kMaxTimeForFullFlood; // Добавляем проверку на возможность затопления
					pumpCheckNeeded = true; //активируем проверку
					swingState = true;

				}
				
				if (digitalRead(kFloatLevelPin) && swingState == true) {
					// Если концевик сработал
					switchPeriph(Periphs::PUMP, false); // выключим насос
					pumpNextSwingTime = currentUnixTime + kSwingOffPeriod; // Заведем таймер на интервал ожидания
					swingState = false;
					pumpCheckNeeded = false;
				} else if (pumpCheckNeeded && currentUnixTime > pumpNextCheckTime) {
					// Если оно долго не сбрасывалось - значит что-то пошло не так
					handleError(ErrorTypes::LOW_WATERLEVEL);
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
}

void eepromRead()
{
	EepromData data;
	eeprom_read_block(static_cast<void*>(&data), 0, sizeof(data));
	pumpOnPeriod = data.pumpOnPeriod;
	pumpOffPeriod = data.pumpOffPeriod;
	lampOnTime.setTime(data.lampOnTime.hours, data.lampOnTime.minutes, 0);
	lampOffTime.setTime(data.lampOffTime.hours, data.lampOffTime.minutes, 0);
}

void eepromWrite()
{
	EepromData data{pumpOnPeriod, pumpOffPeriod, {lampOnTime.hour(), lampOnTime.minute()}, {lampOffTime.hour(), lampOffTime.minute()}};
	eeprom_update_block(static_cast<void*>(&data), 0, sizeof(data));
}

void displayProcedure()
{
	String str1;
	String str2;
	DateTime now = rtc.now();

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
			str1 = "Current Ver";
			str2 = kSWVersion;
			display.clearDisplay();
			display.setCursor(0, 0);
			display.print(str1);
			display.setCursor(0, 18);
			display.print(str2);
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
	}		
}

void firstInit() // Вызывать один раз для того, чтобы перезаписать мусор в EEPROM
{
	rtc.adjust(DateTime(2022, 1, 29, 12, 00, 0));
	lampOnTime.setTime(10, 10, 0);
	lampOffTime.setTime(10, 10, 0);
	pumpOnPeriod = 10;
	pumpOffPeriod = 10;
}



void setup()
{
	Serial.begin(115200);
	rtc.begin();
	pinInit();
	encoderInit();
	oledInit();
	eepromRead();
	switchPeriph(Periphs::GREENLED, true);
	displayMode = DisplayModes::TIME;
	
	DateTime now = rtc.now();
	TimeContainer currentTime{now.hour(), now.minute(), now.second()};
	uint32_t currentUnixTime{now.unixtime()};

	pumpNextSwitchTime = currentUnixTime + (60 * pumpOffPeriod); // Начинаем цикл с положения выкл
	hydroType = HydroTypes::SWING; // TODO Перенести в первичный инцициализатор

}

void loop()
{
	encoder.tick();

	uint64_t currentTime = millis();
	if (currentTime - nextDisplayTime >= kDisplayUpdateTime) {
		nextDisplayTime =currentTime;
		displayProcedure();
	}

	if (currentTime - nextRTCReadTime >= kRTCReadTime) {
		nextRTCReadTime = currentTime;
		checkTime();
	}
}
