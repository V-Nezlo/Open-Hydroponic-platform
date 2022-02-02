//
// TimeContainer.hpp
//
//  Created on: Jan 14, 2022
//      Author: V.Nezlo
//

// Класс для удобной работы со временем
// Операторы < и > сделаны без учета перехода времени через ноль
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

	bool operator == (const TimeContainer &cont1) const&
	{
		return ((_hours == cont1._hours) && (_minutes == cont1._minutes) && (_seconds == cont1._seconds));
	}

	bool operator != (const TimeContainer &cont1) const&
	{
		return ((_hours != cont1._hours) || (_minutes != cont1._minutes) || (_seconds != cont1._seconds));
	}

	bool operator < (const TimeContainer &cont1) const&
	{
		if (_hours < cont1._hours) {
			return true;
		} else if (_hours > cont1._hours) {
			return false;
		} else if (_hours == cont1._hours) {
			if (_minutes < cont1._minutes) {
				return true;
			} else if (_minutes > cont1._minutes) {
				return false;
			} else if (_minutes == cont1._minutes) {
				if (_seconds < cont1._seconds) {
					return true;
				} else {
					return false;
				}
			}
		}
		return false;
	}

	bool operator > (const TimeContainer &cont1) const&
	{
		if (_hours > cont1._hours) {
			return true;
		} else if (_hours < cont1._hours) {
			return false;
		} else if (_hours == cont1._hours) {
			if (_minutes > cont1._minutes) {
				return true;
			} else if (_minutes < cont1._minutes) {
				return false;
			} else if (_minutes == cont1._minutes) {
				if (_seconds > cont1._seconds) {
					return true;
				} else {
					return false;
				}
			}
		}
		return false;
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

	uint8_t hour() const
	{
		return _hours;
	}

	uint8_t minute() const
	{
		return _minutes;
	}

	uint8_t seconds() const
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