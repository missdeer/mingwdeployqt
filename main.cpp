#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <boost/program_options.hpp>

namespace fs = std::filesystem;
namespace po = boost::program_options;

static std::string mingwBinPath;

bool caseInsensitiveCompare(const std::string &str1, const std::string &str2)
{
    return std::equal(str1.begin(), str1.end(), str2.begin(), str2.end(), [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

std::string expandMinGWBinPath(const std::string &fileName)
{
    return mingwBinPath + "\\" + fileName;
}

bool isInMinGWBin(const std::string &fileName)
{
    fs::path filePath = expandMinGWBinPath(fileName);
    return fs::exists(filePath);
}

bool getImportedDLLs(const std::string &fileName, std::vector<std::string> &res, std::ostream &stream)
{
    HANDLE file = CreateFileW(std::wstring(fileName.begin(), fileName.end()).c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_READONLY,
                              nullptr);
    if (INVALID_HANDLE_VALUE == file)
    {
        stream << "Cannot open file " << fileName << ", maybe it's occupied by a running process.\n";
        return false;
    }

    DWORD  fileSize = GetFileSize(file, nullptr);
    LPVOID fileData = HeapAlloc(GetProcessHeap(), 0, fileSize);
    if (fileData == nullptr)
    {
        stream << "Memory allocation failed for reading file.\n";
        CloseHandle(file);
        return false;
    }

    DWORD bytesRead = 0;
    if (!ReadFile(file, fileData, fileSize, &bytesRead, nullptr) || bytesRead != fileSize)
    {
        stream << "Read error: " << fileSize << " bytes expected, but got " << bytesRead << " bytes\n";
        HeapFree(GetProcessHeap(), 0, fileData);
        CloseHandle(file);
        return false;
    }

    auto       *dosHeader          = static_cast<PIMAGE_DOS_HEADER>(fileData);
    auto       *imageNTHeaders     = reinterpret_cast<PIMAGE_NT_HEADERS>(static_cast<unsigned char *>(fileData) + dosHeader->e_lfanew);
    auto       *sectionLocation    = reinterpret_cast<PIMAGE_SECTION_HEADER>(reinterpret_cast<unsigned char *>(imageNTHeaders) + sizeof(DWORD) +
                                                                    sizeof(IMAGE_FILE_HEADER) + imageNTHeaders->FileHeader.SizeOfOptionalHeader);
    const DWORD importDirectoryRVA = imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    PIMAGE_SECTION_HEADER importSection = nullptr;
    for (int i = 0; i < imageNTHeaders->FileHeader.NumberOfSections; i++)
    {
        if (importDirectoryRVA >= sectionLocation->VirtualAddress &&
            importDirectoryRVA < sectionLocation->VirtualAddress + sectionLocation->Misc.VirtualSize)
        {
            importSection = sectionLocation;
        }
        sectionLocation++;
    }

    if (importSection == nullptr)
    {
        stream << "No import section found.\n";
        HeapFree(GetProcessHeap(), 0, fileData);
        CloseHandle(file);
        return false;
    }

    unsigned char *rawOffset        = static_cast<unsigned char *>(fileData) + importSection->PointerToRawData;
    auto          *importDescriptor = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(
        rawOffset + (imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress - importSection->VirtualAddress));

    for (; importDescriptor->Name != 0; importDescriptor++)
    {
        std::string dll(reinterpret_cast<const char *>(rawOffset + (importDescriptor->Name - importSection->VirtualAddress)));
        if (isInMinGWBin(dll))
        {
            res.push_back(dll);
        }
    }
    HeapFree(GetProcessHeap(), 0, fileData);
    CloseHandle(file);
    return true;
}

void getImportedDLLsRecursive(const std::string &filename, std::vector<std::string> &dlls, std::ostream &stream)
{
    std::vector<std::string> res;
    getImportedDLLs(filename, res, stream);
    for (const auto &dll : res)
    {
        auto exists =
            std::any_of(dlls.begin(), dlls.end(), [&dll](const std::string &existingDll) { return caseInsensitiveCompare(dll, existingDll); });
        if (!exists)
        {
            dlls.push_back(dll);
            getImportedDLLsRecursive(expandMinGWBinPath(dll), dlls, stream);
        }
    }
}

bool isQt6(const std::vector<std::string> &dlls)
{
    return std::any_of(dlls.begin(), dlls.end(), [](const std::string &dll) { return caseInsensitiveCompare(dll, "Qt6Core.dll"); });
}

bool isQt5(const std::vector<std::string> &dlls)
{
    return std::any_of(dlls.begin(), dlls.end(), [](const std::string &dll) { return caseInsensitiveCompare(dll, "Qt5Core.dll"); });
}

std::string windeployqtPath(bool qt6)
{
    if (qt6)
    {
        auto windeployqt6 = expandMinGWBinPath("windeployqt-qt6.exe");
        if (fs::exists(windeployqt6))
        {
            return windeployqt6;
        }
    }
    return expandMinGWBinPath("windeployqt.exe");
}

int main(int argc, char *argv[])
{
    po::options_description desc("Options");
    desc.add_options()("help,h", "Display this help message")("version,v", "Display the version number")(
        "mingw-directory,d",
        po::value<std::string>(),
        "Specify MinGW bin directory path. Leave blank to use the directory where the program is put.")(
        "filename", po::value<std::vector<std::string>>(), "File name of MinGW built exe/dll");

    po::positional_options_description p;
    p.add("filename", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        std::cout << desc << "\n";
        return 0;
    }

    if (vm.count("version"))
    {
        std::cout << "Version 1.0" << "\n";
        return 0;
    }

    if (vm.count("mingw-directory"))
    {
        mingwBinPath = vm["mingw-directory"].as<std::string>();
    }
    else
    {
        // get current executable path, not the current working directory
        fs::path currentPath = fs::path(argv[0]);
        mingwBinPath = currentPath.lexically_normal().parent_path().string();
    }
    std::cout << "Specified MinGW bin directory to " << mingwBinPath << "\n";

    if (!vm.count("filename"))
    {
        std::cout << "No files specified.\n";
        return 0;
    }

    std::vector<std::string> args = vm["filename"].as<std::vector<std::string>>();
    for (const auto &arg : args)
    {
        if (!fs::exists(arg))
        {
            std::cout << arg << " doesn't exist.\n";
            continue;
        }

        std::vector<std::string> dlls;
        getImportedDLLsRecursive(arg, dlls, std::cout);
        if (dlls.empty())
        {
            std::cout << "No dlls found in " << arg << ".\n";
            continue;
        }
        std::cout << "Deploying dll:\n";
        for (const auto &dll : dlls)
        {
            auto     src = fs::path(mingwBinPath) / dll;
            fs::path dst = fs::path(arg).parent_path() / dll;
            if (fs::copy_file(src, dst, fs::copy_options::overwrite_existing))
            {
                std::cout << "\t" << dll << "\n";
            }
            else
            {
                std::cout << "\t" << dll << " failed\n";
            }
        }
        bool bIsQt6 = isQt6(dlls);
        bool bIsQt5 = isQt5(dlls);
        if (bIsQt6 || bIsQt5)
        {
            auto windeployqt = windeployqtPath(bIsQt6);
            // Execute the command using a system call or a similar method
            std::string command = windeployqt + " " + arg;
            std::cout << "Executing: " << command << "\n";
            std::system(command.c_str());
        }
    }

    return 0;
}
