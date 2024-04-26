#include <filesystem>
#include <iostream>
#include <map>
#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>

#include "utils.h"

namespace bp = boost::process;
namespace fs = std::filesystem;

// Visual Studio 2015 or newer
// or Clang
#if (defined(_MSC_VER) && _MSC_VER >= 1900) || defined(__clang__)
std::string getProgramFilesX86Path()
{
    char   *programFilesX86Path = nullptr;
    size_t  len                 = 0;
    errno_t err                 = _dupenv_s(&programFilesX86Path, &len, "ProgramFiles(x86)");
    if (err != 0 || programFilesX86Path == nullptr)
    {
        std::cerr << "cannot find ProgramFiles(x86) environment vairable" << std::endl;
        return R"(C:\Program Files (x86))";
    }

    const std::string res(programFilesX86Path);

    // use the path_env
    free(programFilesX86Path);

    return res;
}
#else
std::string getProgramFilesX86Path()
{
    const char *programFilesX86Path = std::getenv("ProgramFiles(x86)");
    if (programFilesX86Path == nullptr)
    {
        std::cerr << "cannot find ProgramFiles(x86) environment variable" << std::endl;
        return R"(C:\Program Files (x86))";
    }

    return {programFilesX86Path};
}
#endif

bool getSDKIncludedDirectories(const std::string &sdkVer, std::vector<std::string> &directories)
{
    const std::string sdkIncludePath = getProgramFilesX86Path() + R"(\Windows Kits\10\Include\)";

    auto useSDKVer = sdkVer;
    if (useSDKVer == "10.0")
    {
        // populate the latest one
        std::vector<std::string> subdirs;
        try
        {
            for (const auto &entry : fs::directory_iterator(sdkIncludePath))
            {
                if (entry.is_directory())
                {
                    subdirs.push_back(entry.path().filename().string());
                }
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << std::endl;
        }
        
        if (!subdirs.empty())
        {
            std::sort(subdirs.begin(), subdirs.end());
            useSDKVer = *subdirs.rbegin();
        }
    }
    directories.push_back(sdkIncludePath + useSDKVer + "\\ucrt");
    directories.push_back(sdkIncludePath + useSDKVer + "\\um");
    directories.push_back(sdkIncludePath + useSDKVer + "\\shared");
    directories.push_back(sdkIncludePath + useSDKVer + "\\winrt");
    directories.push_back(sdkIncludePath + useSDKVer + "\\cppwinrt");
    return true;
}

std::string getMSVS2015IntallPath()
{
    std::string  cmd = R"(reg query "HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Microsoft\VisualStudio\14.0" /v InstallDir)";
    bp::ipstream out_stream;
    bp::child    child_process(cmd, bp::std_out > out_stream);

    std::string line;
    std::string result;
    while (std::getline(out_stream, line))
    {
        result += line + '\n';
    }

    child_process.wait();

    // output should look like below:
    // "       InstallDir    REG_SZ    C:\Program Files (x86)\Microsoft Visual Studio 14.0\Common7\IDE\"
    // extract the last path section
    size_t pos = result.find("InstallDir");
    result     = result.substr(pos);
    boost::algorithm::replace_all(result, "InstallDir", "");
    boost::algorithm::replace_all(result, "REG_SZ", "");
    boost::algorithm::trim(result);
    boost::algorithm::replace_all(result, R"(\Common7\IDE)", "");

    return result;
}

std::string getNewerMSVSInstallPath(const std::string &toolset, bool isLegacy = false)
{
    const std::map<const std::string, const std::string> verMap = {
        {"v140", "[14.0,15.0)"}, // 2015
        {"v141", "[15.0,16.0)"}, // 2017
        {"v142", "[16.0,17.0)"}, // 2019
        {"v143", "[17.0,18.0)"}, // 2022
    };

    const std::string vswherePath = getProgramFilesX86Path() + R"(\Microsoft Visual Studio\Installer\vswhere.exe)";
    std::string       cmd         = "\"" + vswherePath + "\"  -version " + verMap.at(toolset) + " -property installationPath";
    if (isLegacy)
    {
        cmd += " -legacy";
    }
    bp::ipstream out_stream;
    bp::child    child_process(cmd, bp::std_out > out_stream);

    std::string line;
    std::string result;
    while (std::getline(out_stream, line))
    {
        result += line;
    }

    child_process.wait();
    // trim result string
    boost::algorithm::trim(result);
    return result;
}

std::string getNewerMSCVer(const std::string &msvcPath)
{
    // populate the latest one
    std::vector<std::string> subdirs;
    try
    {
        for (const auto &entry : fs::directory_iterator(msvcPath))
        {
            if (entry.is_directory())
            {
                subdirs.push_back(entry.path().filename().string());
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return {};
    }

    std::string mscVer;
    if (!subdirs.empty())
    {
        std::sort(subdirs.begin(), subdirs.end());
        mscVer = *subdirs.rbegin();
    }

    return mscVer;
}

void getVCIncludedDirectories(const std::string &toolset, std::vector<std::string> &directories, bool useOfMFC)
{
    if (toolset == "v140")
    {
        // VS 2015
        const auto installPath = getMSVS2015IntallPath();
        directories.push_back(installPath + R"(\VC\include)");
        if (useOfMFC)
        {
            directories.push_back(installPath + R"(\VC\atlmfc\include)");
        }
    }
    else
    {
        // VS 2017/2019/2022 or higher, earlier versions support is dropped
        const auto        installPath = getNewerMSVSInstallPath(toolset);
        const auto        msvcPath    = installPath + R"(\VC\Tools\MSVC)";
        const std::string mscVer      = getNewerMSCVer(msvcPath);

        if (useOfMFC)
        {
            directories.push_back(installPath + R"(\VC\Tools\MSVC\)" + mscVer + R"(\atlmfc\include)");
        }
        directories.push_back(installPath + R"(\VC\Tools\MSVC\)" + mscVer + R"(\include)");
        directories.push_back(installPath + R"(\VC\Auxiliary\VS\include)");
    }
}

std::string getClPath(const std::string &toolset)
{
    if (toolset == "v140")
    {
        // VS 2015
        const auto installPath = getMSVS2015IntallPath();
        return installPath + R"(\VC\bin\cl.exe)";
    }

    // VS 2017/2019/2022 or higher, earlier versions support is dropped
    const auto        installPath = getNewerMSVSInstallPath(toolset);
    const auto        msvcPath    = installPath + R"(\VC\Tools\MSVC)";
    const std::string mscVer      = getNewerMSCVer(msvcPath);
    if (mscVer.empty())
    {
        std::cerr << "cannot find MSVC version" << std::endl;
        return "cl.exe";
    }

    return installPath + R"(\VC\Tools\MSVC\)" + mscVer + R"(\bin\Hostx64\x64\cl.exe)";
}