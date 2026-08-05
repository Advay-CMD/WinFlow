// Include the library
#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <cstring>
#include <iomanip>
#include <shellapi.h>

// NTFS MFT attribute types
constexpr DWORD ATTR_STANDARD_INFORMATION  = 0x10;
constexpr DWORD ATTR_ATTRIBUTE_LIST        = 0x20;
constexpr DWORD ATTR_FILE_NAME             = 0x30;
constexpr DWORD ATTR_OBJECT_ID             = 0x40;
constexpr DWORD ATTR_SECURITY_DESCRIPTOR   = 0x50;
constexpr DWORD ATTR_VOLUME_NAME           = 0x60;
constexpr DWORD ATTR_VOLUME_INFORMATION    = 0x70;
constexpr DWORD ATTR_DATA                  = 0x80;
constexpr DWORD ATTR_INDEX_ROOT            = 0x90;
constexpr DWORD ATTR_INDEX_ALLOCATION      = 0xA0;
constexpr DWORD ATTR_BITMAP                = 0xB0;
constexpr DWORD ATTR_REPARSE_POINT         = 0xC0;
constexpr DWORD ATTR_LOGGED_UTILITY_STREAM = 0x100;

// While on disk, the last 2 bytes of every 512-byte sector of an MFT record
// are replaced with a fixup (USN) value. This restores the original bytes
// so attribute values can be parsed correctly.
void applyMftFixups(char* record, DWORD recordSize)
{
    WORD fixupOffset = *(WORD*)(record + 0x04);
    WORD fixupCount  = *(WORD*)(record + 0x06);

    if (fixupOffset == 0 || fixupCount == 0)
        return;

    WORD usn = *(WORD*)(record + fixupOffset);
    WORD* fixupArray = reinterpret_cast<WORD*>(record + fixupOffset + 2);

    for (WORD i = 0; i < fixupCount; ++i)
    {
        DWORD sectorTail = (static_cast<DWORD>(i) + 1) * 512 - 2;
        if (sectorTail + 2 > recordSize)
            break;

        WORD* tail = reinterpret_cast<WORD*>(record + sectorTail);
        if (*tail == usn)
            *tail = fixupArray[i];
    }
}

// Read a signed little-endian integer of `size` bytes (max 8), sign-extended.
LONGLONG readSignedLE(const unsigned char* p, int size)
{
    if (size <= 0 || size > 8)
        return 0;

    LONGLONG val = 0;
    for (int i = 0; i < size; ++i)
        val |= static_cast<LONGLONG>(p[i]) << (8 * i);

    // Sign extend when fewer than 8 bytes were used.
    if (size < 8 && (val & (1LL << (8 * size - 1))))
        val -= (1LL << (8 * size));
    return val;
}

// Parse the mapping-pairs array of a non-resident attribute into (LCN, length)
// cluster runs. This is how the MFT's true on-disk layout is described.
bool parseMappingPairs(const unsigned char* p, std::vector<std::pair<LONGLONG, LONGLONG>>& runs)
{
    LONGLONG prevLcn = 0;
    while (*p != 0)
    {
        int lenSize = (*p) >> 4;
        int offSize = (*p) & 0x0F;
        ++p;

        LONGLONG len = readSignedLE(p, lenSize);
        p += lenSize;
        LONGLONG delta = readSignedLE(p, offSize);
        p += offSize;

        prevLcn += delta;
        if (len <= 0 || prevLcn < 0)
            return false;
        runs.push_back({ prevLcn, len });
    }
    return !runs.empty();
}

// Get the runlist of the MFT from the $DATA attribute in record 0.
bool getMftRunlist(const char* data, DWORD recordSize,
                   std::vector<std::pair<LONGLONG, LONGLONG>>& runs)
{
    WORD attrOffset = *(WORD*)(data + 0x14);

    while (attrOffset + 0x10 <= recordSize)
    {
        DWORD attrType = *(DWORD*)(data + attrOffset);
        if (attrType == 0xFFFFFFFF) break;
        DWORD attrLength = *(DWORD*)(data + attrOffset + 0x04);
        if (attrLength < 0x18 || attrOffset + attrLength > recordSize) break;

        if (attrType == 0x80) // $DATA
        {
            BYTE nonResident = *(BYTE*)(data + attrOffset + 0x08);
            if (nonResident == 1)
            {
                WORD mappingOffset = *(WORD*)(data + attrOffset + 0x20);
                if (mappingOffset > 0 && mappingOffset < attrLength)
                {
                    return parseMappingPairs(
                        reinterpret_cast<const unsigned char*>(data + attrOffset + mappingOffset),
                        runs);
                }
            }
        }
        attrOffset += attrLength;
    }
    return false;
}

// Extract the file name from a single MFT record and print it.
// parentFilter: only print entries whose parent directory record matches
// (pass 0xFFFFFFFF to print every record, e.g. for a raw MFT dump).
void printMftRecordFileNames(const char* rawRecord, DWORD recordSize, DWORD recordNumber, DWORD parentFilter, DWORD& fileCount)
{
    // Work on a mutable copy so we can apply the fixup array.
    std::vector<char> record(rawRecord, rawRecord + recordSize);
    char* data = record.data();

    // Valid MFT records start with the "FILE" signature.
    if (memcmp(data, "FILE", 4) != 0)
        return;

    // In root-listing mode skip the first 16 records: they are NTFS system
    // metadata ($MFT, $MFTMirr, $LogFile, ...) whose parent also points to
    // the root and would clutter the output.
    if (parentFilter == 5 && recordNumber < 16)
        return;

    applyMftFixups(data, recordSize);

    WORD firstAttrOffset = *(WORD*)(data + 0x14);

    std::wstring fileName;
    DWORD fileFlags = 0;
    DWORD chosenParent = 0;
    int bestNamespace = 4; // Namespace 0/1 (POSIX/Win32) beats 2 (DOS 8.3)

    DWORD attrOffset = firstAttrOffset;
    while (attrOffset + 0x10 <= recordSize)
    {
        DWORD attrType = *(DWORD*)(data + attrOffset);
        if (attrType == 0xFFFFFFFF)
            break; // End of attribute list marker

        DWORD attrLength = *(DWORD*)(data + attrOffset + 0x04);
        if (attrLength < 0x18 || attrOffset + attrLength > recordSize)
            break;

        if (attrType == ATTR_FILE_NAME)
        {
            BYTE resident = *(BYTE*)(data + attrOffset + 0x08);
            if (resident == 0) // $FILE_NAME is always resident
            {
                DWORD valueLength = *(DWORD*)(data + attrOffset + 0x10);
                WORD  valueOffset = *(WORD*)(data + attrOffset + 0x14);

                if (valueOffset + 0x3A <= attrLength && valueLength >= 0x3A)
                {
                    const char* value = data + attrOffset + valueOffset;

                    // Parent directory reference (low 48 bits = MFT record number)
                    DWORD64 parentRef = 0;
                    memcpy(&parentRef, value + 0x00, 8);
                    DWORD parentRecord = static_cast<DWORD>(parentRef & 0x0000FFFFFFFFFFFFULL);

                    BYTE nameLength = static_cast<BYTE>(value[0x38]);
                    BYTE nameNs     = static_cast<BYTE>(value[0x39]);

                    // Prefer the long (Win32/POSIX) name over the 8.3 DOS name.
                    if (nameLength > 0 &&
                        valueOffset + 0x3A + static_cast<DWORD>(nameLength) * 2 <= attrLength &&
                        nameNs < bestNamespace)
                    {
                        bestNamespace = nameNs;
                        chosenParent = parentRecord;
                        fileName.assign(reinterpret_cast<const wchar_t*>(value + 0x3A), nameLength);
                        fileFlags = *(DWORD*)(value + 0x30);
                    }
                }
            }
        }

        attrOffset += attrLength;
    }

    if (!fileName.empty() && (parentFilter == 0xFFFFFFFF || chosenParent == parentFilter))
    {
        std::wstring type;
        if (fileFlags & FILE_ATTRIBUTE_DIRECTORY)
        {
            type = L"Folder";
        }
        else
        {
            size_t dot = fileName.rfind(L'.');
            if (dot != std::wstring::npos)
            {
                std::wstring ext = fileName.substr(dot + 1);
                for (auto& ch : ext)
                    ch = towlower(ch);
                type = ext + L" file";
            }
            else
            {
                type = L"File";
            }
        }

        std::wcout << L"[" << recordNumber << L"] "
                   << fileName << L"  " << type << L'\n';
        ++fileCount;
    }
}

// Resources
// https://learn.microsoft.com/en-us/windows/console/clearing-the-screen

void cls(HANDLE hConsole)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    SMALL_RECT scrollRect;
    COORD scrollTarget;
    CHAR_INFO fill;

    // Get the number of character cells in the current buffer.
    if (!GetConsoleScreenBufferInfo(hConsole, &csbi))
    {
        return;
    }

    // Scroll the rectangle of the entire buffer.
    scrollRect.Left = 0;
    scrollRect.Top = 0;
    scrollRect.Right = csbi.dwSize.X;
    scrollRect.Bottom = csbi.dwSize.Y;

    // Scroll it upwards off the top of the buffer with a magnitude of the entire height.
    scrollTarget.X = 0;
    scrollTarget.Y = (SHORT)(0 - csbi.dwSize.Y);

    // Fill with empty spaces with the buffer's default text attribute.
    fill.Char.UnicodeChar = TEXT(' ');
    fill.Attributes = csbi.wAttributes;

    // Do the scroll
    ScrollConsoleScreenBuffer(hConsole, &scrollRect, NULL, scrollTarget, &fill);

    // Move the cursor to the top left corner too.
    csbi.dwCursorPosition.X = 0;
    csbi.dwCursorPosition.Y = 0;

    SetConsoleCursorPosition(hConsole, csbi.dwCursorPosition);
}

int main()
{
    // Making the Wstrings a and b
    std::wstring a;
    std::wstring b;
    // Print the Welcome. This can be changed later to stylish text. or we will see.
    std::wcout << L"Welcome to WinFlow Explorer CLI. Enter commands to navigate, or type help.\n";
    while(true) {
        std::wcout << L"\n>>> ";
        std::wcout.flush();
        // Function to receive BOTH A and B and C
        std::wstring line;
        std::getline(std::wcin, line);

        std::wistringstream iss(line);

        std::wstring a, b, c;
        iss >> a >> b >> c;   // If there's no second word, b stays empty, and the same logic for c.
        if(a == L"clear" || a == L"cls") 
        {
            // Resources 
            // https://learn.microsoft.com/en-us/windows/console/clearing-the-screen

            HANDLE hStdout;
            hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
            cls(hStdout);
        }
        else if (a == L"exit" || a == L"quit")
        {
            // Just exit. No abort()
            return 0;
        }
        else if (a == L"ls" || a == L"dir")
        {
            // Resources
            // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw
            // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextfilea

            // To-do: Make it from MFT - Severity = HIGH
            // Why use this method?
            // Lets say we have a LOT of files on a slow computer.
            // Now if the user opens, the app will be most likely to freeze/hang.
            // Therefore, ADD the listing one by one. Most modern PC's won't notice the difference

            if (b.empty())
            {
                std::wcout << L"Usage: ls <folder>/<drive>\n";
                continue;
            }
            // First, make the variable
            WIN32_FIND_DATAW fd;
            // Set up a handle
            HANDLE handle_file = FindFirstFileExW(
                (b + L"\\*").c_str(), // Basically search the directory
                FindExInfoBasic, // Use basic to speed operations
                &fd, // Provide the variable
                FindExSearchNameMatch, // Basically the "Normal"
                nullptr, // null over here, according to the document
                0 //Same here
            );
            // TO-DO: Error Handling - Severity = Low - It can scan the current dir.
            do 
            {
                // Keep scanning anď printing
                std::wcout << fd.cFileName << L'\n';
            } 
            while (FindNextFileW(handle_file, &fd)); //Keep getting new files
            std::wcout << L"\n"; // Finally, print a newline for neatify
            FindClose(handle_file); // Finally, CLOSE the handle when done.
        }
        else if(a == L"lsa") 
        {
            // Usage: lsa                      -> list C:\ root
            //        lsa C:\path              -> list any directory
            //        lsa D:\                  -> list D:\ root
            //        lsa [recordCount]         -> raw MFT scan
            std::wstring listPath = L"C:\\";
            DWORD recordsToRead = 256;
            DWORD parentFilter = 5;

            bool countExplicit = false;
            if (!b.empty())
            {
                try
                {
                    recordsToRead = static_cast<DWORD>(std::stoul(b));
                    parentFilter = 0xFFFFFFFF;
                    countExplicit = true;
                }
                catch (...)
                {
                    std::wstring p = b;
                    if (p.size() >= 2 && p[1] == L':')
                    {
                        wchar_t d = p[0];
                        if (d >= L'a' && d <= L'z')
                            d = static_cast<wchar_t>(d - L'a' + L'A');
                        if (d >= L'A' && d <= L'Z')
                        {
                            // Could be "C:" or "C:\Windows\System32"
                            if (p.size() > 2)
                                listPath = p;
                            else
                                listPath = d + std::wstring(L":\\");
                        }
                    }
                }
                if (recordsToRead == 0)
                    recordsToRead = 256;
            }

            if (!c.empty())
            {
                try
                {
                    recordsToRead = static_cast<DWORD>(std::stoul(c));
                    countExplicit = true;
                }
                catch (...) {}
                if (recordsToRead == 0)
                    recordsToRead = 256;
            }

            std::wstring driveLetter = listPath.substr(0, 1);

            // ── Fast listing (uses NTFS index B-tree) ──────────────
            if (parentFilter == 5)
            {
                std::wstring rootPath = listPath;
                if (rootPath.back() != L'\\')
                    rootPath += L'\\';
                rootPath += L"*";
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileExW(
                    rootPath.c_str(), FindExInfoBasic, &fd,
                    FindExSearchNameMatch, nullptr, 0);

                if (hFind == INVALID_HANDLE_VALUE)
                {
                    std::wcout << L"Failed to list " << driveLetter
                               << L":\\. Error: " << GetLastError() << L'\n';
                    continue;
                }

                struct Entry { std::wstring name; bool isDir; ULONGLONG size; };
                std::vector<Entry> entries;

                do
                {
                    std::wstring name = fd.cFileName;
                    if (name == L"." || name == L"..")
                        continue;

                    bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    ULONGLONG sz = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                    entries.push_back({ name, isDir, sz });

                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);

                std::wcout << L'\n'
                           << L"  " << std::left << std::setw(32) << L"Name"
                           << L"Type\n"
                           << L"  " << std::wstring(32, L'-') << L"----\n";

                for (const auto& e : entries)
                {
                    std::wstring type;
                    if (e.isDir)
                    {
                        type = L"Folder";
                    }
                    else
                    {
                        size_t dot = e.name.rfind(L'.');
                        if (dot != std::wstring::npos)
                        {
                            std::wstring ext = e.name.substr(dot + 1);
                            for (auto& ch : ext)
                                ch = towlower(ch);
                            type = ext + L" file";
                        }
                        else
                        {
                            type = L"File";
                        }
                    }

                    std::wcout << L"  " << std::left << std::setw(32) << e.name
                               << type << L'\n';
                }

                std::wcout << L'\n' << entries.size() << L" entries in "
                           << listPath << L'\n';
                continue;
            }

            // ── Raw MFT scan ────────────────────────────────────────────
            std::wstring volumePath = L"\\\\.\\" + driveLetter + L":";
            HANDLE hVol = CreateFileW(volumePath.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hVol == INVALID_HANDLE_VALUE)
            {
                std::wcout << L"Failed to open " << driveLetter
                           << L":. Error: " << GetLastError() << L'\n';
                continue;
            }

            NTFS_VOLUME_DATA_BUFFER nvd = {};
            DWORD got = 0;
            if (!DeviceIoControl(hVol, FSCTL_GET_NTFS_VOLUME_DATA,
                                 NULL, 0, &nvd, sizeof(nvd), &got, NULL))
            {
                std::wcout << L"FSCTL_GET_NTFS_VOLUME_DATA failed. Error: "
                           << GetLastError() << L'\n';
                CloseHandle(hVol);
                continue;
            }

            LONGLONG mftOffset = nvd.MftStartLcn.QuadPart * nvd.BytesPerCluster;
            DWORD recSize = nvd.BytesPerFileRecordSegment;

            std::wcout << L"Scanning MFT from " << driveLetter
                       << L": (" << recSize << L" bytes/record)...\n";

            // Read record 0 ($MFT) to get the runlist.
            std::vector<char> rec0(recSize);
            LARGE_INTEGER pos{};
            pos.QuadPart = mftOffset;
            DWORD got0 = 0;
            if (!SetFilePointerEx(hVol, pos, NULL, FILE_BEGIN) ||
                !ReadFile(hVol, rec0.data(), recSize, &got0, NULL) || got0 < recSize)
            {
                std::wcout << L"Failed to read record 0. Error: "
                           << GetLastError() << L'\n';
                CloseHandle(hVol);
                continue;
            }

            applyMftFixups(rec0.data(), recSize);

            std::vector<std::pair<LONGLONG, LONGLONG>> runs;
            if (!getMftRunlist(rec0.data(), recSize, runs))
            {
                std::wcout << L"Failed to parse MFT runlist. Falling back to linear read.\n";

                // Linear fallback: read chunks from MftStartLcn
                LONGLONG validLength = nvd.MftValidDataLength.QuadPart;
                const LONGLONG CHUNK = 4 * 1024 * 1024;
                std::vector<char> chunk(static_cast<size_t>(CHUNK));
                DWORD fileCount = 0;
                DWORD parsed = 0;
                LONGLONG streamOffset = 0;

                pos.QuadPart = mftOffset;
                while (parsed < recordsToRead && streamOffset < validLength)
                {
                    LONGLONG toRead = (validLength - streamOffset < CHUNK)
                                      ? validLength - streamOffset : CHUNK;
                    if (!SetFilePointerEx(hVol, pos, NULL, FILE_BEGIN)) break;
                    DWORD br = 0;
                    if (!ReadFile(hVol, chunk.data(), static_cast<DWORD>(toRead), &br, NULL) || br == 0) break;

                    for (DWORD off = 0; off + recSize <= br; off += recSize, ++parsed)
                    {
                        if (parsed >= recordsToRead) break;
                        DWORD rn = static_cast<DWORD>((streamOffset + off) / recSize);
                        printMftRecordFileNames(chunk.data() + off, recSize, rn, parentFilter, fileCount);
                    }
                    streamOffset += br;
                    pos.QuadPart += br;
                }

                std::wcout << L'\n' << parsed << L" records scanned, "
                           << fileCount << L" entries found.\n";
                CloseHandle(hVol);
                continue;
            }

            LONGLONG validLength = nvd.MftValidDataLength.QuadPart;
            const LONGLONG CHUNK = 4 * 1024 * 1024;
            std::vector<char> chunk(static_cast<size_t>(CHUNK));
            DWORD fileCount = 0;
            DWORD parsedRecords = 0;
            LONGLONG streamOffset = 0;
            bool keepGoing = true;

            for (const auto& run : runs)
            {
                if (!keepGoing) break;
                LONGLONG lcn = run.first;
                LONGLONG remaining = run.second * nvd.BytesPerCluster;
                if (remaining <= 0) continue;

                while (remaining > 0 && keepGoing)
                {
                    LONGLONG toRead = (remaining < CHUNK) ? remaining : CHUNK;
                    pos.QuadPart = lcn * nvd.BytesPerCluster;
                    if (!SetFilePointerEx(hVol, pos, NULL, FILE_BEGIN)) { keepGoing=false; break; }
                    DWORD bytesRead = 0;
                    if (!ReadFile(hVol, chunk.data(), static_cast<DWORD>(toRead), &bytesRead, NULL) || bytesRead == 0) { keepGoing=false; break; }

                    for (DWORD off = 0; off + recSize <= bytesRead; off += recSize)
                    {
                        LONGLONG rso = streamOffset + off;
                        if (rso >= validLength || parsedRecords >= recordsToRead) { keepGoing=false; break; }
                        DWORD rn = static_cast<DWORD>(rso / recSize);
                        printMftRecordFileNames(chunk.data() + off, recSize, rn, parentFilter, fileCount);
                        ++parsedRecords;
                    }
                    streamOffset += bytesRead;
                    remaining -= bytesRead;
                    lcn += bytesRead / nvd.BytesPerCluster;
                }
            }

            std::wcout << L'\n' << parsedRecords << L" records scanned, "
                       << fileCount << L" entries found.\n";
            CloseHandle(hVol);
        }
        else if(a == L"mkdir") 
        {
            // Resources
            // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createdirectoryw

            if (b.empty())
            {
                std::wcout << L"Usage: mkdir <folder>\n";
                continue;
            }

            // Create the directory
            if(CreateDirectoryW((b + L"\\").c_str(), nullptr)) 
            {
                // Print that the Operation Succeeded
                std::wcout << L"Operation Succeeded.\n";
            }
            else
            {
                // Get the last error
                switch (GetLastError())
                {
                    case ERROR_ALREADY_EXISTS:
                        std::wcout << L"Folder already exists.\n";
                        break;

                    case ERROR_PATH_NOT_FOUND:
                        std::wcout << L"Parent folder doesn't exist.\n";
                        break;

                    case ERROR_ACCESS_DENIED:
                        std::wcout << L"Access denied.\n";
                        break;

                    default:
                        std::wcout << L"Unknown error.\n";
                }
            }
        } 
        else if (a == L"rmdir") 
        {
            // Resources
            // https://stackoverflow.com/questions/213392/what-is-the-win32-api-function-to-use-to-delete-a-folder
            
            if (b.empty())
            {
                std::wcout << L"Usage: rmdir <folder>\n";
                continue;
            }

            // SHFileOperation requires a double-null-terminated string.
            std::wstring path = b;
            path.push_back(L'\0');

            // Form the structure
            SHFILEOPSTRUCTW op = {};
            op.wFunc = FO_DELETE;
            op.pFrom = path.c_str();
            op.fFlags = FOF_NOCONFIRMATION |
                        FOF_NOERRORUI |
                        FOF_SILENT;

            // Execute
            int result = SHFileOperationW(&op);

            // Error Handling -> If 0, say Operation has succeeded. or else Falied.
            if (result == 0)
            {
                std::wcout << L"Operation succeeded.\n";
            }
            else
            {
                std::wcout << L"Failed to remove folder. Error: " << result << L'\n';
            }
        }
        else if (a == L"remove") 
        {
            // Resources
            // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-deletefilew

            if(b.empty()) 
            {
                std::wcout << L"Usage: remove <path><filename>\n";
                continue;
            }
            // Delete the file
            if (DeleteFileW((b).c_str())) 
            {
                // It's Successful
                std::wcout << "Success!\n";
            }
            else 
            {
                // Error Handling - I hope the cases makes it VERY understandable
                switch (GetLastError())
                {
                case ERROR_FILE_NOT_FOUND:
                    std::wcout << L"File not found.\n";
                    break;
                
                case ERROR_ACCESS_DENIED:
                    std::wcout << L"Access is Denied.\n"; 
                    break;   

                default:
                    std::wcout << L"Unknown Error.\n";
                    break;
                }
            }
        }
        else if (a == L"create") 
        {
            // Resources
            // https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew
            // https://learn.microsoft.com/en-us/windows/win32/api/handleapi/nf-handleapi-closehandle

            if(b.empty()) 
            {
                std::wcout << L"Usage create <path><filename>\n";
                continue;
            }

            // Create the file
            HANDLE handle_file = CreateFileW(
                (b).c_str(), // Provide the name
                GENERIC_READ | GENERIC_WRITE, // read and write
                0, // empty
                nullptr, // No thanks microsoft
                CREATE_NEW, // Create a NEW file
                FILE_ATTRIBUTE_NORMAL, // Normal is the default, and most commonly used
                nullptr // No templates for me
            );
            // Error handling - I hope it is easy to understand.
            if (handle_file == INVALID_HANDLE_VALUE)
            {
                std::wcout << L"Error: " << GetLastError() << L'\n';
            }
            else
            {
                std::wcout << L"Created successfully.\n";
                CloseHandle(handle_file);
            }
        }
        /* else if (a == L"copyfile") 
        {
            // Resources
            // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfilew

            // Basically COPY. Easy.
            if(CopyFileW(b.c_str(), c.c_str(), FALSE)) 
            {
                std::wcout << L"Successful. Copied.\n";
            }
            else 
            {
                std::wcout << L"Error. Here are some more details: " << GetLastError() << L"\n";
            }
        } */
        else if (a == L"copy")
        {
            // MULTIPLE resources
            // Cannot be listed

            // Comments will be added later
            if (b.empty() || c.empty())
            {
                std::wcout << L"Usage: copy <source> <destination>\n";
                continue;
            }

            auto CopyDirectory = [&](auto&& self,
                                     const std::wstring& source,
                                     const std::wstring& destination) -> bool
            {
                CreateDirectoryW(destination.c_str(), nullptr);

                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileExW(
                    (source + L"\\*").c_str(),
                    FindExInfoBasic,
                    &fd,
                    FindExSearchNameMatch,
                    nullptr,
                    0);

                if (hFind == INVALID_HANDLE_VALUE)
                    return false;

                do
                {
                    std::wstring name = fd.cFileName;

                    if (name == L"." || name == L"..")
                        continue;

                    std::wstring src = source + L"\\" + name;
                    std::wstring dst = destination + L"\\" + name;

                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        if (!self(self, src, dst))
                        {
                            FindClose(hFind);
                            return false;
                        }
                    }
                    else
                    {
                        if (!CopyFileW(src.c_str(), dst.c_str(), FALSE))
                        {
                            FindClose(hFind);
                            return false;
                        }
                    }

                } while (FindNextFileW(hFind, &fd));

                FindClose(hFind);
                return true;
            };

            DWORD attr = GetFileAttributesW(b.c_str());

            if (attr == INVALID_FILE_ATTRIBUTES)
            {
                std::wcout << L"Source does not exist.\n";
                continue;
            }

            if (attr & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (CopyDirectory(CopyDirectory, b, c))
                    std::wcout << L"Directory copied successfully.\n";
                else
                    std::wcout << L"Failed to copy directory. Error: " << GetLastError() << L'\n';
            }
            else
            {
                if (CopyFileW(b.c_str(), c.c_str(), FALSE))
                    std::wcout << L"File copied successfully.\n";
                else
                    std::wcout << L"Failed to copy file. Error: " << GetLastError() << L'\n';
            }
        }
        else 
        {
            // Its actually NOT a command, print that.
            std::wcout << L"\n" << L"Not a command" << L"\n";
        }
    }
}