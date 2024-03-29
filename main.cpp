﻿#include <filesystem>
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

#include "utils.h"

namespace bp = boost::process;
namespace po = boost::program_options;
namespace fs = std::filesystem;

namespace
{
    std::string outputDirectory;
    std::string target;
    bool        fileInserted = false;
} // namespace

bool parseVcxprojFile(const std::string &filePath, std::ofstream &ofs)
{
    fs::path vcxprojFilePath(fs::absolute(fs::path(filePath)));
    if (!fs::exists(vcxprojFilePath))
    {
        std::cerr << filePath << " not exists" << std::endl;
        return false;
    }
    fs::path          vcxprojParentDirPath = vcxprojFilePath.parent_path().lexically_normal();
    const std::string vcxprojParentDirStr  = boost::algorithm::replace_all_copy(vcxprojParentDirPath.string(), "\\", "/");

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
    std::transform(
        additionalIncludedDirectories.begin(), additionalIncludedDirectories.end(), additionalIncludedDirectories.begin(), [](const auto &str) {
            fs::path p(str);
            return boost::algorithm::replace_all_copy(p.lexically_normal().string(), "\\", "/");
        });

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
    std::transform(systemIncludedDirectories.begin(), systemIncludedDirectories.end(), systemIncludedDirectories.begin(), [](const auto &str) {
        fs::path p(str);
        return boost::algorithm::replace_all_copy(p.lexically_normal().string(), "\\", "/");
    });

    const std::string cppCmd = R"(  "command": "\"clang++.exe\" -x c++ \")";
    const std::string cCmd   = R"(  "command": "\"clang.exe\" -x c \")";
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
            const std::string cppFileStr = boost::algorithm::replace_all_copy(cppFile, "\\", "/");
            const bool        isCpp      = !boost::algorithm::iends_with(cppFile, ".c");

            if (!fileInserted)
            {
                fileInserted = true;
            }
            else
            {
                ofs << ",\n";
            }
            ofs << "{\n"
                << R"(  "directory": ")" << vcxprojParentDirStr << R"(",)" << "\n"
                << R"(  "file": ")" << cppFileStr << R"(",)" << "\n"
                << (isCpp ? cppCmd : cCmd) << cppFileStr << R"(\" -fsyntax-only )";
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
                ofs << R"( -isystem\")" << systemIncludedDirectory << R"(\" )";
            }
            for (const auto &additionalIncludedDirectory : additionalIncludedDirectories)
            {
                ofs << R"( -I\")" << additionalIncludedDirectory << R"(\" )";
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
    std::string inputFile;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h",
                       "produce help message")("target,t", po::value<std::string>(&target)->default_value("Release|x64"), "set build target")(
        "output-directory,o", po::value<std::string>(&outputDirectory)->default_value("."), "output directory")(
        "input-path,i",
        po::value<std::string>(&inputFile)->default_value(""),
        "input a .sln or .vcxproj file path, or a directory path contains .sln/.vcxproj files");

    po::variables_map varMap;
    po::store(po::parse_command_line(argc, argv, desc), varMap);
    po::notify(varMap);

    if (varMap.count("help"))
    {
        std::cout << desc << std::endl;
        return 1;
    }

    if (inputFile.empty())
    {
        std::cerr << "No input file is specified." << std::endl;
        return 1;
    }

    std::vector<std::string> inputFiles;
    fs::path                 inputPath(inputFile);
    if (fs::is_directory(inputPath))
    {
        // traverse this directory, find all .sln files
        for (const auto &entry : std::filesystem::directory_iterator(inputPath))
        {
            if (entry.is_regular_file() && boost::iends_with(entry.path().string(), ".sln"))
            {
                inputFiles.push_back(entry.path().string());
            }
        }
    }
    else if (fs::is_regular_file(inputPath) && (boost::iends_with(inputFile, ".sln") || boost::iends_with(inputFile, ".vcxproj")))
    {
        inputFiles.push_back(inputFile);
    }

    if (inputFiles.empty())
    {
        fs::path inputPath(inputFile);
        if (fs::is_directory(inputPath))
        {
            // traverse this directory, find all .vcxproj files
            for (const auto &entry : std::filesystem::directory_iterator(inputPath))
            {
                if (entry.is_regular_file() && boost::iends_with(entry.path().string(), ".vcxproj"))
                {
                    inputFiles.push_back(entry.path().string());
                }
            }
        }
    }
    if (inputFiles.empty())
    {
        std::cerr << "No .sln or .vcxproj file is found." << std::endl;
        return 1;
    }

    target = "'$(Configuration)|$(Platform)'=='" + target + "'";

    fs::path outputPath(outputDirectory + "\\compile_commands.json");
    outputPath = fs::absolute(outputPath).lexically_normal();
    std::ofstream ofs(outputPath.string());

    if (!ofs.is_open())
    {
        std::cerr << "Error opening file " << outputPath.string() << std::endl;
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

    std::cout << outputPath.string() << " is written" << std::endl;

    return 0;
}
