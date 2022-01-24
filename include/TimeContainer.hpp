//
// TimeContainer.hpp
//
//  Created on: Jan 14, 2022
//      Author: V.Nezlo
//

// Класс для удобной работы со временем
// Операторы < и > сделаны с учетом того, что разница между операндами не больше 60 минут
// TODO: Переписать

class TimeContainer {

private:
uint8_t _hours;
uint8_t _minutes;
uint8_t _seconds;

public:
	TimeContainer(uint8_t aHour, uint8_t aMinutes, uint8_t aSeconds = 0) :
	_hours{aHour},
	_minutes{aMinutes},
	_seconds{aSeconds}
	{

	}

	bool operator == (const TimeContainer &cont1)
	{
		return ((_hours == cont1._hours) && (_minutes == cont1._minutes) && (_seconds == cont1._seconds));
	}

	bool operator != (const TimeContainer &cont1)
	{
		return ((_hours != cont1._hours) || (_minutes != cont1._minutes) || (_seconds != cont1._seconds));
	}

	bool operator < (const TimeContainer &cont1)
	{
		uint8_t hour1{0};
		uint8_t hour2{0};

		if (_hours == 0 && cont1._hours != 0) {
			hour1 = _hours + 12;
			hour2 = cont1._hours - 12;
		} else if (cont1._hours == 0 && _hours != 0) {
			hour1 = _hours - 12;
			hour2 = cont1._hours + 12;
		} else {
			hour1 = _hours;
			hour2 = cont1._hours;
		}

		if (hour1 < hour2) {
			return true;
		} else if (hour1 > hour2) {
			return false;
		} else if (hour1 == hour2) {
			if (_minutes < cont1._minutes) {
				return true;
			} else if (_minutes > cont1._minutes) {
				return false;
			} else if (_minutes = cont1._minutes) {
				if (_seconds < cont1._seconds) {
					return true;
				} else {
					return false;
				}
			}
		}
	}

	bool operator > (const TimeContainer &cont1)
	{
		uint8_t hour1{0};
		uint8_t hour2{0};

		if (_hours == 0 && cont1._hours != 0) {
			hour1 = _hours + 12;
			hour2 = cont1._hours - 12;
		} else if (cont1._hours == 0 && _hours != 0) {
			hour1 = _hours - 12;
			hour2 = cont1._hours + 12;
		} else {
			hour1 = _hours;
			hour2 = cont1._hours;
		}

		if (hour1 > hour2) {
			return true;
		} else if (hour1 < hour2) {
			return false;
		} else if (hour1 == hour2) {
			if (_minutes > cont1._minutes) {
				return true;
			} else if (_minutes < cont1._minutes) {
				return false;
			} else if (_minutes = cont1._minutes) {
				if (_seconds > cont1._seconds) {
					return true;
				} else {
					return false;
				}
			}
		}
	}

	void setTime(uint8_t aHour, uint8_t aMinutes, uint8_t aSeconds)
	{
	    _hours = aHour;
	    _minutes = aMinutes;
	    _seconds = aSeconds;
	}

	void getTime(uint8_t &aHours, uint8_t &aMinutes, uint8_t &aSeconds)
	{
		aHours = _hours;
		aMinutes = _minutes;
		aSeconds = _seconds;
	}

	uint8_t hour()
	{
		return _hours;
	}

	uint8_t minute()
	{
		return _minutes;
	}

	uint8_t seconds()
	{
		return _seconds;
	}

	void addTime(uint8_t aMinutes)
	{
		uint8_t curMinutes = _minutes;
		if (curMinutes + aMinutes < 60) {
			_minutes += aMinutes;
		} else {
			_minutes = (curMinutes + aMinutes) - 60;
			if (_hours < 23) {
				++_hours;
			} else {
				_hours = 0;
			}
		}
	}
};