/*
 * huzzah.c
 *
 * Created: 1/23/2018 9:16:27 AM
 *  Author: charl
 */ 

#include "huzzah.h"
#include "mcp23017.h"

BOOL wifi_enabled(void)
{
	uint8_t result;
	mcp23017_readPort(&result, MCP23017_PORTB);
	result &= (1 << WIFI_POWER_ENABLE);
	return result;
}

void wifi_reset(BOOL reset)
{
	if(reset) // assert reset low
	{
		mcp23017_set(WIFI_RESET, LOW);
	}
	else // de-assert reset high
	{
		mcp23017_set(WIFI_RESET, HIGH);
	}
}

void wifi_power(BOOL on)
{
	mcp23017_set(WIFI_POWER_ENABLE, on);
}
