#include <Windows.h>

#include <cstdio>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

static QString mingwBinPath;

QString expandMinGWBinPath(const QString &fileName)
{
    return QDir(mingwBinPath).absoluteFilePath(fileName);
}

bool isInMinGWBin(const QString &fileName)
{
    return QFile::exists(expandMinGWBinPath(fileName));
}

bool getImportedDLLs(const QString &fileName, QStringList &res, QTextStream &stream)
{
    HANDLE file =
        CreateFileW(fileName.toStdWString().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, nullptr);
    if (INVALID_HANDLE_VALUE == file)
    {
        stream << "Cannot open file " << fileName << ", maybe it's occupied by a running process.\n";
        return false;
    }

    DWORD  fileSize = GetFileSize(file, nullptr);
    LPVOID fileData = HeapAlloc(GetProcessHeap(), 0, fileSize);

    DWORD bytesRead = 0;
    ReadFile(file, fileData, fileSize, &bytesRead, nullptr);
    CloseHandle(file);
    if (bytesRead != fileSize)
    {
        stream << "Read error: " << fileSize << " is expected, but got " << bytesRead << " bytes\n";
        HeapFree(GetProcessHeap(), 0, fileData);
        return false;
    }

    auto *dosHeader      = static_cast<PIMAGE_DOS_HEADER>(fileData);
    auto *imageNTHeaders = (PIMAGE_NT_HEADERS)((unsigned char *)fileData + dosHeader->e_lfanew);

    auto       *sectionLocation    = (PIMAGE_SECTION_HEADER)((unsigned char *)imageNTHeaders + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                                                    imageNTHeaders->FileHeader.SizeOfOptionalHeader);
    const DWORD importDirectoryRVA = imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    // get the last section, it's import section
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

    unsigned char *rawOffset = (unsigned char *)fileData + importSection->PointerToRawData;
    auto          *importDescriptor =
        (PIMAGE_IMPORT_DESCRIPTOR)(rawOffset + (imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress -
                                                importSection->VirtualAddress));

    for (; importDescriptor->Name != 0; importDescriptor++)
    {
        auto dll = QString((const char *)(rawOffset + (importDescriptor->Name - importSection->VirtualAddress)));
        if (isInMinGWBin(dll))
        {
            res.append(dll);
        }
    }
    HeapFree(GetProcessHeap(), 0, fileData);
    return true;
}

void getImportedDLLsRecursive(const QString &filename, QStringList &dlls, QTextStream &stream)
{
    QStringList res;
    getImportedDLLs(filename, res, stream);
    for (const auto &dll : res)
    {
        if (!dlls.contains(dll, Qt::CaseInsensitive))
        {
            dlls.append(dll);
            getImportedDLLsRecursive(expandMinGWBinPath(dll), dlls, stream);
        }
    }
}

bool isQt6(const QStringList &dlls)
{
    for (const auto &dll : dlls)
    {
        if (dll.compare("Qt6Core.dll", Qt::CaseInsensitive) == 0)
        {
            return true;
        }
    }
    return false;
}

QString windeployqtPath(bool qt6)
{
    if (qt6)
    {
        auto windeployqt6 = expandMinGWBinPath(QStringLiteral("windeployqt-qt6.exe"));
        if (QFile::exists(windeployqt6))
        {
            return windeployqt6;
        }
    }
    return expandMinGWBinPath(QStringLiteral("windeployqt.exe"));
}

int main(int argc, char *argv[])
{
    QCoreApplication   app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("mingwdeployqt", "Command line client deploy MinGW built files"));
    parser.addPositionalArgument(QCoreApplication::translate("mingwdeployqt", "[filename]"),
                                 QCoreApplication::tr("File name of MinGW built exe/dll"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption mingwBinDir(QStringList() << QStringLiteral("d") << QStringLiteral("mingw-directory"),
                                   QCoreApplication::translate("mingwdeployqt", "Specify MinGW bin directory path. Leave blank to use the directory where mingwdeployqt.exe put."),
                                   QStringLiteral("directory-path"));
    parser.addOption(mingwBinDir);

    parser.process(app);

    QTextStream stream(stdout);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    stream.setCodec("UTF-8");
#else
    stream.setEncoding(QStringConverter::Utf8);
#endif

    if (parser.isSet(mingwBinDir))
    {
        mingwBinPath = parser.value(mingwBinDir);
    }
    else
    {
        mingwBinPath = QCoreApplication::applicationDirPath();
    }
    stream << "Specified MinGW bin directory to " << QDir::toNativeSeparators(mingwBinPath) << "\n";

    auto args = parser.positionalArguments();
    if (args.isEmpty())
    {
        parser.showHelp();
        return 0;
    }
    for (const auto &arg : args)
    {
        if (!QFile::exists(arg))
        {
            stream << arg << " doesn't exists.\n";
            continue;
        }

        QStringList dlls;
        getImportedDLLsRecursive(arg, dlls, stream);
        stream << "Deploying dll:\n";
        for (const auto &dll : dlls)
        {
            auto      src = QDir::toNativeSeparators(expandMinGWBinPath(dll));
            QFileInfo fileInfo(arg);
            QDir      dir = fileInfo.absoluteDir();
            auto      dst = QDir::toNativeSeparators(dir.absoluteFilePath(dll));
            if (::CopyFileW(src.toStdWString().c_str(), dst.toStdWString().c_str(), FALSE))
            {
                stream << "\t" << dll << "\n";
            }
            else
            {
                stream << "\t" << dll << " failed\n";
            }
        }
        auto windeployqt = windeployqtPath(isQt6(dlls));
        QProcess process;
        process.start(windeployqt, QStringList() << arg);
        process.waitForFinished();
    }

    return 0;
}
