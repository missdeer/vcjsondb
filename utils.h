#pragma once

std::string getProgramFilesX86Path();
bool        getSDKIncludedDirectories(const std::string &sdkVer, std::vector<std::string> &directories);
std::string getMSVS2015IntallPath();
std::string getNewerMSVSInstallPath(const std::string &toolset, bool isLegacy = false);
void        getVCIncludedDirectories(const std::string &toolset, std::vector<std::string> &directories);