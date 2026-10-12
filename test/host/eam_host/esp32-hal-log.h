#pragma once
// WString.cpp logs an out-of-memory with log_e/log_w. The host has no log sink;
// the test reads the String's own validity instead.
#define log_e(...) ((void)0)
#define log_w(...) ((void)0)
