#include "log.h"

Logger::Logger()
	: log_ { "deshade.txt" }
{
}

void Logger::Flush()
{
	log_.flush();
}

Logger& Logger::Get()
{
	static Logger logger_;
	return logger_;
}

void Log(const char *string)
{
	Logger& log = Logger::Get();
	for (; *string; log << *string++)
	{
		if (*string != '%')
		{
			continue;
		}

		if (*(string + 1) == '%')
		{
			++string;
		}
		else
		{
			break;
		}
	}

	log.Flush();
}
