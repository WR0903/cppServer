#pragma once
#include "libserver/console/console.h"

class RobotConsoleHttp :public ConsoleCmd
{
public:
	void RegisterHandler() override;
	void HandleHelp() override;

private:	
	void HandleRequest(std::vector<std::string>& params);
};
