#include <log.hpp>

void more_test_code();

int main() {
    logger::setMinLevel(LogLevel::DEBUG);

    LOG(INFO)("This is an info message.");
    LOG(DEBUG)("This is a debug message.");
    LOG(WARN)("This is a warning message.");
    LOG(ERROR)("This is an error message.");

    int value = 42;
    LOG(INFO)("The answer to life, the universe, and everything is %d", value);

    more_test_code();

    return 0;
}

void more_test_code() {
    LOG(INFO)("Logging from more_test_code function.");
}