#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/process.hpp>
#include <boost/program_options.hpp>
#include <rapidxml.hpp>

namespace bp = boost::process;
namespace po = boost::program_options;
namespace fs = std::filesystem;

namespace
{
    std::string outputDirectory;
    std::string target;
    bool        fileInserted = false;
} // namespace

std::string getProgramFilesX86Path()
{
    char       *programFilesX86Path = nullptr;
    size_t      len                 = 0;
    errno_t     err                 = _dupenv_s(&programFilesX86Path, &len, "ProgramFiles(x86)");
    std::string sdkIncludePath;
    if (err != 0 || programFilesX86Path == nullptr)
    {
        std::cerr << "cannot find ProgramFiles(x86) environment vairable" << std::endl;
        return R"(C:\Program Files (x86))";
    }

    std::string res(programFilesX86Path);

    // use the path_env
    free(programFilesX86Path);

    return res;
}

bool getSDKIncludedDirectories(const std::string &sdkVer, std::vector<std::string> &directories)
{
    std::string sdkIncludePath = getProgramFilesX86Path() + R"(\Windows Kits\10\Include\)";

    auto useSDKVer = sdkVer;
    if (useSDKVer == "10.0")
    {
        // populate the latest one
        std::vector<std::string> subdirs;
        for (const auto &entry : fs::directory_iterator(sdkIncludePath))
        {
            if (entry.is_directory())
            {
                subdirs.push_back(entry.path().filename().string());
            }
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

    std::string vswherePath = getProgramFilesX86Path() + R"(\Microsoft Visual Studio\Installer\vswhere.exe)";
    std::string cmd         = "\"" + vswherePath + "\"  -version " + verMap.at(toolset) + " -property installationPath";
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
        result += line + '\n';
    }

    child_process.wait();

    return result;
}

void getVCIncludedDirectories(const std::string &toolset, std::vector<std::string> &directories)
{
    if (toolset == "v140")
    {
        // VS 2015
        auto installPath = getMSVS2015IntallPath();
        directories.push_back(installPath + R"(\VC\include)");
        directories.push_back(installPath + R"(\VC\atlmfc\include)");
    }
    else
    {
        // VS 2017/2019/2022 or higher, earlier versions support is dropped
        auto installPath = getNewerMSVSInstallPath(toolset);
        auto msvcPath    = installPath + R"(\VC\Tools\MSVC)";

        // populate the latest one
        std::vector<std::string> subdirs;
        for (const auto &entry : fs::directory_iterator(msvcPath))
        {
            if (entry.is_directory())
            {
                subdirs.push_back(entry.path().filename().string());
            }
        }

        std::string mscVer;
        if (!subdirs.empty())
        {
            std::sort(subdirs.begin(), subdirs.end());
            mscVer = *subdirs.rbegin();
        }

        directories.push_back(installPath + R"(\VC\Tools\MSVC\)" + mscVer + R"(\include)");
        directories.push_back(installPath + R"(\VC\Tools\MSVC\)" + mscVer + R"(\atlmfc\include)");
        directories.push_back(installPath + R"(\VC\Auxiliary\VS\include)");
    }
}

bool parseVcxprojFile(const std::string &filePath, std::ofstream &ofs)
{
    fs::path vcxprojFilePath(fs::absolute(fs::path(filePath)));
    if (!fs::exists(vcxprojFilePath))
    {
        std::cerr << filePath << " not exists" << std::endl;
        return false;
    }
    fs::path vcxprojParentDirPath = vcxprojFilePath.parent_path();

    std::ifstream     file(filePath);
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    buffer.push_back('\0');

    rapidxml::xml_document<> doc;
    doc.parse<0>(buffer.data());

    auto *rootNode = doc.first_node("Project");
    if (!rootNode)
    {
        std::cerr << "cannot find root Project node" << std::endl;
        return false;
    }

    std::string sdkVer            = "10.0";
    auto       *propertyGroupNode = rootNode->first_node("PropertyGroup");
    for (; propertyGroupNode != nullptr; propertyGroupNode = propertyGroupNode->next_sibling("PropertyGroup"))
    {
        auto *labelAttr = propertyGroupNode->first_attribute("Label");
        if (!labelAttr)
        {
            continue;
        }
        const std::string label(labelAttr->value(), labelAttr->value_size());
        if (label == "Globals")
        {
            // get WindowsTargetPlatformVersion
            auto *sdkVerNode = propertyGroupNode->first_node("WindowsTargetPlatformVersion");
            if (sdkVerNode)
            {
                sdkVer = std::string(sdkVerNode->value(), sdkVerNode->value_size());
            }
            continue;
        }
        if (label == "Configuration")
        {
            auto *conditionAttr = propertyGroupNode->first_attribute("Condition");
            if (!conditionAttr)
            {
                continue;
            }
            if (target == conditionAttr->value())
            {
                break;
            }
        }
    }
    if (!propertyGroupNode)
    {
        std::cerr << "cannot find PropertyGroup node with matched target " << target << std::endl;
        return false;
    }

    auto *charsetNode = propertyGroupNode->first_node("CharacterSet");
    if (!charsetNode)
    {
        std::cerr << "cannot find CharacterSet node" << std::endl;
        return false;
    }
    const std::string charset(charsetNode->value(), charsetNode->value_size());

    auto *toolsetNode = propertyGroupNode->first_node("PlatformToolset");
    if (!toolsetNode)
    {
        std::cerr << "cannot find PlatformToolset node" << std::endl;
        return false;
    }
    const std::string toolset(toolsetNode->value(), toolsetNode->value_size());

    bool  isDLL     = false;
    auto *isDLLNode = propertyGroupNode->first_node("ConfigurationType");
    if (isDLLNode)
    {
        const std::string configurationType(isDLLNode->value(), isDLLNode->value_size());
        isDLL = (configurationType == "DynamicLibrarys");
    }

    bool  useOfMFC     = false;
    auto *useOfMFCNode = propertyGroupNode->first_node("UseOfMfc");
    if (useOfMFCNode)
    {
        const std::string useOfMFCValue(useOfMFCNode->value(), useOfMFCNode->value_size());
        useOfMFC = useOfMFCValue == "Dynamic";
    }

    auto *itemDefinitionGroupNode = rootNode->first_node("ItemDefinitionGroup");
    for (; itemDefinitionGroupNode != nullptr; itemDefinitionGroupNode = itemDefinitionGroupNode->next_sibling("ItemDefinitionGroup"))
    {
        auto *conditionAttr = itemDefinitionGroupNode->first_attribute("Condition");
        if (!conditionAttr)
        {
            continue;
        }
        if (target == conditionAttr->value())
        {
            break;
        }
    }
    if (!itemDefinitionGroupNode)
    {
        std::cerr << "cannot find ItemDefinitionGroup node with matched target " << target << std::endl;
        return false;
    }

    auto *clCompileNode = itemDefinitionGroupNode->first_node("ClCompile");
    if (!clCompileNode)
    {
        std::cerr << "cannot find definition ClCompile node from " << filePath << std::endl;
        return false;
    }

    std::string languageStandard;
    auto       *languageStandardNode = clCompileNode->first_node("LanguageStandard");
    if (languageStandardNode)
    {
        languageStandard = std::string(languageStandardNode->value(), languageStandardNode->value_size());
    }
    const std::map<std::string, std::string> languageStandardMap = {
        {"stdcpp11", "-std=c++11"},
        {"stdcpp14", "-std=c++14"},
        {"stdcpp17", "-std=c++17"},
        {"stdcpp20", "-std=c++20"},
        {"stdcpp23", "-std=c++23"},
        {"stdcpplatest", "-std=c++2b"},
    };
    auto iter = languageStandardMap.find(languageStandard);
    if (languageStandardMap.end() != iter)
    {
        languageStandard = iter->second;
    }
    else
    {
        languageStandard = "-std=c++14"; // by default, for MSVC 2015
    }

    bool  isMultiThread      = false;
    auto *runtimeLibraryNode = clCompileNode->first_node("RuntimeLibrary");
    if (runtimeLibraryNode)
    {
        const std::string runtimeLibrary(runtimeLibraryNode->value(), runtimeLibraryNode->value_size());
        isMultiThread = (runtimeLibrary == "MultiThreadedDLL" || runtimeLibrary == "MultiThreaded");
    }

    auto *additionalIncludedDirectoriesNode = clCompileNode->first_node("AdditionalIncludeDirectories");
    if (!additionalIncludedDirectoriesNode)
    {
        std::cerr << "cannot find AdditionalIncludeDirectories node" << std::endl;
        return false;
    }
    const std::string additionalIncludedDirectoriesStr(additionalIncludedDirectoriesNode->value(), additionalIncludedDirectoriesNode->value_size());
    std::vector<std::string> additionalIncludedDirectories;
    boost::algorithm::split(additionalIncludedDirectories, additionalIncludedDirectoriesStr, boost::is_any_of(";"));
    auto iterRemove = std::remove_if(additionalIncludedDirectories.begin(), additionalIncludedDirectories.end(), [](const auto &str) {
        return boost::algorithm::starts_with(str, "%(");
    });
    additionalIncludedDirectories.erase(iterRemove, additionalIncludedDirectories.end());

    auto *preprocessorDefinitionsNode = clCompileNode->first_node("PreprocessorDefinitions");
    if (!preprocessorDefinitionsNode)
    {
        std::cerr << "cannot find PreprocessorDefinitions node" << std::endl;
        return false;
    }
    const std::string        preprocessorDefinitionsStr(preprocessorDefinitionsNode->value(), preprocessorDefinitionsNode->value_size());
    std::vector<std::string> preprocessorDefinitions;
    boost::algorithm::split(preprocessorDefinitions, preprocessorDefinitionsStr, boost::is_any_of(";"));
    iterRemove = std::remove_if(
        preprocessorDefinitions.begin(), preprocessorDefinitions.end(), [](const auto &str) { return boost::algorithm::starts_with(str, "%("); });
    preprocessorDefinitions.erase(iterRemove, preprocessorDefinitions.end());

    for (auto &preprocessorDefinition : preprocessorDefinitions)
    {
        boost::algorithm::replace_all(preprocessorDefinition, R"(\)", R"(\\)");
        boost::algorithm::replace_all(preprocessorDefinition, R"(")", R"(/\")");
    }

    std::vector<std::string> systemIncludedDirectories;
    getVCIncludedDirectories(toolset, systemIncludedDirectories);
    getSDKIncludedDirectories(sdkVer, systemIncludedDirectories);

    for (auto *itemGroupNode = rootNode->first_node("ItemGroup"); itemGroupNode != nullptr; itemGroupNode = itemGroupNode->next_sibling("ItemGroup"))
    {
        auto *clCompileNode = itemGroupNode->first_node("ClCompile");
        for (; clCompileNode != nullptr; clCompileNode = clCompileNode->next_sibling("ClCompile"))
        {
            auto *includeAttr = clCompileNode->first_attribute("Include");
            if (!includeAttr)
            {
                continue;
            }
            const std::string cppFile(includeAttr->value(), includeAttr->value_size());
            const bool        isCpp  = !boost::algorithm::iends_with(cppFile, ".c");
            const std::string cppCmd = R"(  "command": "\"clang++.exe\" -x c++ \")";
            const std::string cCmd   = R"(  "command": "\"clang.exe\" -x c \")";

            if (!fileInserted)
            {
                fileInserted = true;
            }
            else
            {
                ofs << ",\n";
            }
            ofs << "{\n"
                << R"(  "directory": ")" << boost::algorithm::replace_all_copy(vcxprojParentDirPath.string(), "\\", "/") << R"(",)"
                << "\n"
                << R"(  "file": ")" << boost::algorithm::replace_all_copy(cppFile, "\\", "/") << R"(",)"
                << "\n"
                << (isCpp ? cppCmd : cCmd) << boost::algorithm::replace_all_copy(cppFile, "\\", "/") << R"(\" -fsyntax-only )";
            if (isCpp)
            {
                ofs << languageStandard << " ";
            }

            for (const auto &preprocessorDefinition : preprocessorDefinitions)
            {
                ofs << R"( \"-D)" << preprocessorDefinition << R"(\" )";
            }
            if (charset == "Unicode")
            {
                ofs << R"( \"-DUNICODE\" \"-D_UNICODE\" )";
            }
            if (useOfMFC)
            {
                ofs << R"( \"-D_AFXDLL\" )";
            }
            if (isMultiThread)
            {
                ofs << R"( \"-D_MT\" )";
            }
            if (isDLL)
            {
                ofs << R"( \"-D_DLL\" )";
            }
            for (const auto &systemIncludedDirectory : systemIncludedDirectories)
            {
                ofs << R"( -isystem\")" << boost::algorithm::replace_all_copy(systemIncludedDirectory, "\\", "/") << R"(\" )";
            }
            for (const auto &additionalIncludedDirectory : additionalIncludedDirectories)
            {
                ofs << R"( -I\")" << boost::algorithm::replace_all_copy(additionalIncludedDirectory, "\\", "/") << R"(\" )";
            }

            ofs << "\"\n}";
        }
    }

    return true;
}

bool parseSlnFile(const std::string &filePath, std::ofstream &ofs)
{
    fs::path slnFilePath(fs::absolute(fs::path(filePath)));
    if (!fs::exists(slnFilePath))
    {
        std::cerr << filePath << " not exists" << std::endl;
        return false;
    }
    std::ifstream ifs(filePath);
    if (!ifs.is_open())
    {
        std::cerr << "Error opening file: " << filePath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    std::string input = buffer.str();

    ifs.close();

    fs::path    slnParentDirPath = slnFilePath.parent_path();
    std::regex  vcxproj_regex("\"([^\"]+\\.vcxproj)\"");
    std::smatch match;

    while (std::regex_search(input, match, vcxproj_regex))
    {
        fs::path vcxprojPath(match[1].str());
        fs::path vcxprojFullPath = slnParentDirPath / vcxprojPath;
        vcxprojFullPath          = vcxprojFullPath.lexically_normal();

        parseVcxprojFile(vcxprojFullPath.string(), ofs);

        input = match.suffix().str();
    }
    return true;
}

int main(int argc, char *argv[])
{
    std::vector<std::string> inputFiles;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h",
                       "produce help message")("target,t", po::value<std::string>(&target)->default_value("Release|x64"), "set build target")(
        "output-directory,o", po::value<std::string>(&outputDirectory)->default_value("."), "output directory")(
        "input-files,i", po::value<std::vector<std::string>>(&inputFiles)->multitoken(), "input .sln or .vcxproj files path");

    po::variables_map varMap;
    po::store(po::parse_command_line(argc, argv, desc), varMap);
    po::notify(varMap);

    if (varMap.count("help"))
    {
        std::cout << desc << std::endl;
        return 1;
    }

    if (inputFiles.empty())
    {
        std::cerr << "No input file is specified." << std::endl;
        return 1;
    }

    target = "'$(Configuration)|$(Platform)'=='" + target + "'";

    std::ofstream ofs(outputDirectory + "\\compile_commands.json");

    if (!ofs.is_open())
    {
        std::cerr << "Error opening file " << outputDirectory << "\\compile_commands.json" << std::endl;
        return 1;
    }
    ofs << "[\n";
    for (const auto &file : inputFiles)
    {
        if (boost::algorithm::iends_with(file, ".vcxproj"))
        {
            parseVcxprojFile(file, ofs);
        }
        if (boost::algorithm::iends_with(file, ".sln"))
        {
            parseSlnFile(file, ofs);
        }
    }

    ofs << "\n]\n";
    ofs.close();

    std::cout << outputDirectory + "\\compile_commands.json is written" << std::endl;

    return 0;
}
