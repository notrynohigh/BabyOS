"""
UTC timestamp <-> struct conversion.
Matches b_mod_utc.c / b_mod_utc.h behavior.
Base year: 2000-01-01 00:00:00 UTC
"""

DAY = 86400
BEGYEAR = 2000


def _is_leap_year(year: int) -> bool:
    return (year % 400 == 0) or ((year % 4 == 0) and (year % 100 != 0))


def _year_days(year: int) -> int:
    return 366 if _is_leap_year(year) else 365


def _calendar_month_days(year: int, month: int) -> int:
    """Return number of days in the given month."""
    if month < 1 or month > 12:
        return 0
    if month == 2:
        return 28 + (1 if _is_leap_year(year) else 0)
    if month in (1, 3, 5, 7, 8, 10, 12):
        return 31
    return 30


def bUTC2Struct(utc: int) -> dict:
    """
    Convert UTC (seconds since 2000-01-01 00:00:00) to datetime struct.
    Returns dict with: year, month, day, week, hour, minute, second
    week: 1=Monday ... 7=Sunday  (original: ((days%7)+5)%7)+1)
    """
    sec_in_day = utc % DAY
    second = sec_in_day % 60
    minute = (sec_in_day % 3600) // 60
    hour = sec_in_day // 3600

    days = utc // DAY
    week = (((days % 7) + 5) % 7) + 1

    year = BEGYEAR
    while days >= _year_days(year):
        days -= _year_days(year)
        year += 1

    month = 1
    while days >= _calendar_month_days(year, month):
        days -= _calendar_month_days(year, month)
        month += 1

    day = days + 1
    return dict(year=year, month=month, day=day, week=week,
                hour=hour, minute=minute, second=second)


def bStruct2UTC(dt: dict) -> int:
    """
    Convert datetime struct (year,month,day,hour,minute,second) to UTC.
    Returns 0 if the date/time is invalid.
    """
    year = dt['year']
    month = dt['month']
    day = dt['day']
    hour = dt['hour']
    minute = dt['minute']
    second = dt['second']

    if not (year < 2999 and 1 <= month <= 12 and 1 <= day <= 31
            and hour <= 23 and minute <= 59 and second <= 59):
        return 0

    seconds = ((hour * 60) + minute) * 60 + second
    days = day - 1
    m = month - 1
    while m > 0:
        days += _calendar_month_days(year, m)
        m -= 1

    y = year - 1
    while y >= BEGYEAR:
        days += _year_days(y)
        y -= 1

    return seconds + days * DAY
