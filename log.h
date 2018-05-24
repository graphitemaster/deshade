#ifndef LOG_H
#define LOG_H

#include <fstream>

struct Logger
{
	static Logger& Get();

	template<typename T>
	void operator<<(const T& value)
	{
		log_ << value;
	}

	void Flush();

private:
	Logger();
	std::ofstream log_;
};

void Log(const char *string);

template<typename T, typename... Ts>
void Log(const char *string, T value, Ts&&... args)
{
	for (Logger& log = Logger::Get(); *string; log << *string++)
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
			log << value;
			return Log(string + 1, std::forward<Ts>(args)...);
		}
	}
}

#endif
