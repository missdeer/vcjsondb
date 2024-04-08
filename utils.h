#pragma once

#include <string>
#include <vector>

bool        getSDKIncludedDirectories(const std::string &sdkVer, std::vector<std::string> &directories);
void        getVCIncludedDirectories(const std::string &toolset, std::vector<std::string> &directories, bool useOfMFC);
std::string getClPath(const std::string &toolset);