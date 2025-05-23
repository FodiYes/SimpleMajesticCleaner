#define _CRT_SECURE_NO_WARNINGS
#include <locale.h>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <ctime>
#include <map>

namespace fs = std::filesystem;

struct CleanResult {
    std::string path;
    bool success;
    std::string reason;
};

std::map<std::string, std::vector<CleanResult>> cleanResults;

void logMessage(const std::string& message) {
    std::cout << "[LOG CLEANER] " << message << std::endl;
}

void addResult(const std::string& directory, const std::string& file, bool success, const std::string& reason = "") {
    CleanResult result;
    result.path = file;
    result.success = success;
    result.reason = reason;
    cleanResults[directory].push_back(result);
}


void saveResultsToFile() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    char filename[100];
    std::strftime(filename, sizeof(filename), "log_cleaner_report_%Y%m%d_%H%M%S.dat", std::localtime(&time));

    std::ofstream reportFile(filename);
    if (!reportFile.is_open()) {
        logMessage("Не удалось создать файл отчета!");
        return;
    }

    reportFile << "=== ОТЧЕТ ОБ ОЧИСТКЕ ЛОГОВ ===" << std::endl;
    reportFile << "Дата и время: ";
    reportFile << std::ctime(&time) << std::endl;

    for (const auto& [directory, results] : cleanResults) {
        reportFile << "ДИРЕКТОРИЯ: " << directory << std::endl;

        for (const auto& result : results) {
            if (result.success) {
                reportFile << result.path << " - удалось удалить" << std::endl;
            }
            else {
                reportFile << result.path << " - не удалось удалить";
                if (!result.reason.empty()) {
                    reportFile << " (" << result.reason << ")";
                }
                reportFile << std::endl;
            }
        }
        reportFile << std::endl;
    }

    reportFile.close();
    logMessage("Отчет сохранен в файл: " + std::string(filename));
}

std::string getKnownFolderPath(int csidl) {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, csidl, NULL, 0, path))) {
        return std::string(path);
    }
    return "";
}

void cleanDirectory(const std::string& directory, bool recursive = true) {
    try {
        if (!fs::exists(directory)) {
            return;
        }

        size_t count = 0;
        size_t failed = 0;

        for (const auto& entry : fs::directory_iterator(directory)) {
            try {
                std::string entryPath = entry.path().string();
                std::string fileName = entry.path().filename().string();

                if (fs::is_directory(entry) && recursive) {
                    cleanDirectory(entryPath, true);
                    try {
                        fs::remove(entry.path());
                        count++;
                        addResult(directory, fileName, true);
                    }
                    catch (const std::exception& e) {
                        failed++;
                        addResult(directory, fileName, false, e.what());
                    }
                }
                else {
                    try {
                        fs::remove(entry.path());
                        count++;
                        addResult(directory, fileName, true);
                    }
                    catch (const std::exception& e) {
                        failed++;
                        addResult(directory, fileName, false, e.what());
                    }
                }
            }
            catch (const std::exception& e) {
                failed++;
                addResult(directory, "неизвестный файл", false, e.what());
            }
        }

        logMessage("Очищено в " + directory + ": " + std::to_string(count) + " файлов, не удалось: " + std::to_string(failed));
    }
    catch (const std::exception& e) {
        logMessage("Ошибка при очистке " + directory + ": " + e.what());
    }
}

void cleanEventLogs() {
    logMessage("Очистка журналов событий Windows...");

    const char* eventLogs[] = {
        "Application", "System", "Security", "Setup",
        "HardwareEvents", "Internet Explorer", "Key Management Service",
        "Windows PowerShell"
    };

    for (const auto& logName : eventLogs) {
        std::string command = "wevtutil cl \"" + std::string(logName) + "\" 2>NUL";
        system(command.c_str());
        logMessage("Журнал " + std::string(logName) + " очищен");
    }
}

void cleanWMILogs() {
    logMessage("Очистка логов WMI...");
    system("net stop winmgmt /y >NUL 2>NUL");

    cleanDirectory("%SystemRoot%\\System32\\wbem\\Repository");
    cleanDirectory("%SystemRoot%\\System32\\wbem\\Logs");

    system("net start winmgmt >NUL 2>NUL");
}

void cleanDirectXLogs() {
    logMessage("Очистка логов DirectX...");
    cleanDirectory("%SystemRoot%\\System32\\LogFiles\\DxDiag");
}

void cleanNvidiaLogs() {
    logMessage("Очистка логов NVIDIA...");

    std::vector<std::string> nvidiaPaths = {
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\NVIDIA Corporation\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\NVIDIA Corporation\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\NVIDIA\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\NVIDIA\\",
    };

    for (const auto& path : nvidiaPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "Logs\\", true);
            cleanDirectory(path + "NvTelemetry\\", true);
            cleanDirectory(path + "DisplayDriver\\NvTelemetry\\", true);
            cleanDirectory(path + "DXCache");
            cleanDirectory(path + "GLCache");
            cleanDirectory("C:\\ProgramData\\NVIDIA Corporation\\Drs\\", true);
        }
    }
}

void cleanTempFiles() {
    logMessage("Очистка временных файлов...");

    std::vector<std::string> tempPaths = {
        getKnownFolderPath(CSIDL_INTERNET_CACHE),
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\Temp\\",
        getKnownFolderPath(CSIDL_WINDOWS) + "\\Temp\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\Temp\\"
    };

    char tempPath[MAX_PATH];
    if (GetTempPathA(MAX_PATH, tempPath)) {
        tempPaths.push_back(std::string(tempPath));
    }

    for (const auto& path : tempPaths) {
        cleanDirectory(path, true);
    }
}

void cleanWindowsUpdateLogs() {
    logMessage("Очистка логов обновлений Windows...");

    std::vector<std::string> updateLogPaths = {
        getKnownFolderPath(CSIDL_WINDOWS) + "\\SoftwareDistribution\\DataStore\\Logs\\",
        getKnownFolderPath(CSIDL_WINDOWS) + "\\SoftwareDistribution\\Download\\",
        getKnownFolderPath(CSIDL_WINDOWS) + "\\Logs\\WindowsUpdate\\"
    };

    system("net stop wuauserv /y >NUL 2>NUL");

    for (const auto& path : updateLogPaths) {
        cleanDirectory(path, true);
    }

    system("net start wuauserv >NUL 2>NUL");
}

void cleanInstallerLogs() {
    logMessage("Очистка логов установщика Windows...");
    cleanDirectory(getKnownFolderPath(CSIDL_WINDOWS) + "\\Logs\\CBS\\");
    cleanDirectory(getKnownFolderPath(CSIDL_WINDOWS) + "\\Logs\\DISM\\");
    cleanDirectory(getKnownFolderPath(CSIDL_WINDOWS) + "\\Inf\\");
}

void cleanProgramLogs() {
    logMessage("Очистка журналов программ...");

    std::vector<std::string> programLogPaths = {
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\CrashDumps\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\Microsoft\\Windows\\WER\\ReportArchive\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\Microsoft\\Windows\\WER\\ReportQueue\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\Microsoft\\Windows\\WER\\ReportArchive\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\Microsoft\\Windows\\WER\\ReportQueue\\"
    };

    for (const auto& path : programLogPaths) {
        cleanDirectory(path, true);
    }
}

void cleanDNSCache() {
    logMessage("Очистка кэша DNS...");
    system("ipconfig /flushdns >NUL 2>NUL");
}

void cleanAntiCheatLogs() {
    logMessage("Очистка логов античитов...");

    std::vector<std::string> eacPaths = {
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\EasyAntiCheat\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\EasyAntiCheat\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\EasyAntiCheat\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES_COMMON) + "\\EasyAntiCheat\\"
    };

    for (const auto& path : eacPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "Logs\\", true);
            logMessage("Очищены логи EasyAntiCheat");
        }
    }

    std::vector<std::string> bePaths = {
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\BattlEye\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\BattlEye\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\Common Files\\BattlEye\\"
    };

    for (const auto& path : bePaths) {
        if (fs::exists(path)) {
            cleanDirectory(path, true);
            logMessage("Очищены логи BattlEye");
        }
    }

    std::vector<std::string> vanguardPaths = {
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\Riot Vanguard\\Logs\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\VALORANT\\Saved\\Logs\\"
    };

    for (const auto& path : vanguardPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path, true);
            logMessage("Очищены логи Vanguard (Valorant)");
        }
    }

    std::vector<std::string> faceitPaths = {
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\FACEIT\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\FACEITClient\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\FACEIT AC\\"
    };

    for (const auto& path : faceitPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "Logs\\", true);
            cleanDirectory(path + "log\\", true);
            logMessage("Очищены логи FACEIT");
        }
    }

    std::vector<std::string> eseaPaths = {
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\ESEA\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\ESEA\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\ESEA Client\\"
    };

    for (const auto& path : eseaPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "Logs\\", true);
            logMessage("Очищены логи ESEA");
        }
    }

    std::vector<std::string> pbPaths = {
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\PunkBuster\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\PunkBuster\\"
    };

    for (const auto& path : pbPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path, true);
            logMessage("Очищены логи PunkBuster");
        }
    }

    std::vector<std::string> gameguardPaths = {
        "C:\\GameGuard\\",
        getKnownFolderPath(CSIDL_PROGRAM_FILES) + "\\GameGuard\\"
    };

    for (const auto& path : gameguardPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "Logs\\", true);
            cleanDirectory(path, false);
            logMessage("Очищены логи GameGuard");
        }
    }

    std::vector<std::string> xigncodesPaths = {
        "C:\\Windows\\xhunter1.sys",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\XIGNCODE\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\XIGNCODE\\"
    };

    for (const auto& path : xigncodesPaths) {
        if (fs::exists(path)) {
            if (fs::is_directory(path)) {
                cleanDirectory(path, true);
            }
            else {
                try {
                    fs::remove(path);
                    addResult(fs::path(path).parent_path().string(), fs::path(path).filename().string(), true);
                }
                catch (const std::exception& e) {
                    addResult(fs::path(path).parent_path().string(), fs::path(path).filename().string(), false, e.what());
                }
            }
            logMessage("Очищены логи Xigncode3");
        }
    }
}

void cleanAltVLogs() {
    logMessage("Очистка логов ALT:V...");

    std::vector<std::string> altVPaths = {
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\altv\\",
        getKnownFolderPath(CSIDL_COMMON_APPDATA) + "\\altv\\",
        getKnownFolderPath(CSIDL_LOCAL_APPDATA) + "\\Alt-V Multiplayer\\",
        "C:\\altv-data\\"
    };

    for (const auto& path : altVPaths) {
        if (fs::exists(path)) {
            cleanDirectory(path + "logs\\", true);
            cleanDirectory(path + "crashdumps\\", true);
            cleanDirectory(path + "cache\\", true);
            cleanDirectory(path + "ac-logs\\", true);
            cleanDirectory(path + "anticheat\\logs\\", true);
            logMessage("Очищены логи ALT:V");
        }
    }

    std::string documentsPath = getKnownFolderPath(CSIDL_PERSONAL);
    if (!documentsPath.empty()) {
        std::string altVDocsPath = documentsPath + "\\altv\\";
        if (fs::exists(altVDocsPath)) {
            cleanDirectory(altVDocsPath + "logs\\", true);
            cleanDirectory(altVDocsPath + "debug\\", true);
            logMessage("Очищены логи ALT:V в документах");
        }
    }
}


bool isRunAsAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID AdministratorsGroup;

    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &AdministratorsGroup)) {
        if (!CheckTokenMembership(NULL, AdministratorsGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(AdministratorsGroup);
    }

    return isAdmin != 0;
}


void printCenteredBox(const std::string& text, int width) {
    if (width < 2) return;

    std::string border(width, '=');
    std::cout << "+" << border << "+" << std::endl;

    int innerWidth = width; 
    int padding = (innerWidth - (int)text.size()) / 2;
    int paddingRight = innerWidth - (int)text.size() - padding;

    std::cout << "|"
        << std::string(padding, ' ')
        << text
        << std::string(paddingRight, ' ')
        << "|" << std::endl;

    std::cout << "+" << border << "+" << std::endl;
}

bool runRestartCmd(const std::string& description, const std::string& command, const std::string& component = "Перезапуск") {
    logMessage(description + "...");
    int result = system(command.c_str());

    if (result == 0) {
        addResult(component, description, true);
        logMessage(description + " - УСПЕШНО");
        return true;
    }
    else {
        addResult(component, description, false, "Код возврата: " + std::to_string(result));
        logMessage(description + " - НЕ УДАЛОСЬ (код: " + std::to_string(result) + ")");
        return false;
    }
}


void restartProgs() {
    logMessage("=== Перезапуск процессов и служб ===");



    runRestartCmd("Остановка службы WMI", "net stop winmgmt /y >NUL 2>NUL");
    runRestartCmd("Запуск службы WMI", "net start winmgmt >NUL 2>NUL");

    runRestartCmd("Остановка службы Windows Update", "net stop wuauserv /y >NUL 2>NUL");
    runRestartCmd("Запуск службы Windows Update", "net start wuauserv >NUL 2>NUL");

    runRestartCmd("Очистка кэша DNS", "ipconfig /flushdns >NUL 2>NUL");

    runRestartCmd("Перезапуск всех устройств (NVIDIA и др.)", "pnputil /restart-device * >NUL 2>NUL");

    runRestartCmd("Завершение explorer.exe", "taskkill /F /IM explorer.exe >NUL 2>NUL");
    runRestartCmd("Запуск explorer", "start explorer");

    logMessage("=== Перезапуск завершён ===");
}

void CleanCheatsFolders() {

    runRestartCmd("удаление папок nightfall", "rmdir /s /q \"C:\\Scripts\" \"C:\\Temp\"");
    /*
    когда нибудь
    runRestartCmd("удаление папок Ret9", "");

    */
};

void cleanShellBags() {
    logMessage("Очистка ShellBags (история папок)...");

    runRestartCmd("Очистка HKCU\\Software\\Microsoft\\Windows\\Shell\\Bags", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\Shell\\Bags\" /f >NUL 2>NUL", "ShellBags");
    runRestartCmd("Очистка HKCU\\Software\\Microsoft\\Windows\\Shell\\BagMRU", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\Shell\\BagMRU\" /f >NUL 2>NUL", "ShellBags");

    runRestartCmd("Очистка Local Settings Bags", "reg delete \"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\Bags\" /f >NUL 2>NUL", "ShellBags");
    runRestartCmd("Очистка Local Settings BagMRU", "reg delete \"HKCU\\Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\Shell\\BagMRU\" /f >NUL 2>NUL", "ShellBags");

    runRestartCmd("Очистка HKLM Bags", "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Shell\\Bags\" /f >NUL 2>NUL", "ShellBags");
    runRestartCmd("Очистка HKLM BagMRU", "reg delete \"HKLM\\SOFTWARE\\Microsoft\\Windows\\Shell\\BagMRU\" /f >NUL 2>NUL", "ShellBags");

    runRestartCmd("Очистка StreamMRU", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\Shell\\StreamMRU\" /f >NUL 2>NUL", "ShellBags");
    runRestartCmd("Очистка ComDlg32", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ComDlg32\" /f >NUL 2>NUL", "ShellBags");

    runRestartCmd("Очистка RecentDocs", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RecentDocs\" /f >NUL 2>NUL", "ShellBags");
    runRestartCmd("Очистка RunMRU", "reg delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\RunMRU\" /f >NUL 2>NUL", "ShellBags");

    logMessage("ShellBags полностью очищены");
}

int main() {
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
    setlocale(LC_ALL, "Russian");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleOutputCP(866);

    std::string message = "WLG - Клинер перед проверкой";
    int boxWidth = 58;

    printCenteredBox(message, boxWidth);

    if (!isRunAsAdmin()) {
        std::cout << "ОШИБКА: Программа требует запуска с правами администратора!" << std::endl;
        std::cout << "Пожалуйста, перезапустите программу от имени администратора." << std::endl;
        system("pause");
        return 1;
    }

    std::cout << "Программа запущена с правами администратора." << std::endl;
    std::cout << "ВНИМАНИЕ: Эта программа удалит все системные логи и временные файлы!" << std::endl;
    std::cout << "Продолжить? (y/n): ";

    char choice;
    std::cin >> choice;

    if (choice != 'y' && choice != 'Y') {
        std::cout << "Операция отменена пользователем." << std::endl;
        system("pause");
        return 0;
    }

    std::cout << "Начинаем очистку системы..." << std::endl;

    cleanEventLogs();
    cleanWMILogs();
    cleanDirectXLogs();
    cleanNvidiaLogs();
    cleanWindowsUpdateLogs();
    cleanInstallerLogs();
    cleanTempFiles();
    cleanProgramLogs();
    cleanDNSCache();
    cleanAntiCheatLogs();
    cleanAltVLogs();
    CleanCheatsFolders();
    cleanShellBags();
    restartProgs();


    saveResultsToFile();

    std::cout << "\nОчистка завершена!" << std::endl;
    std::cout << "Все системные логи и временные файлы были удалены." << std::endl;
    std::cout << "Отчет о проделанной работе сохранен в .dat файл." << std::endl;
    system("pause");

    return 0;
}