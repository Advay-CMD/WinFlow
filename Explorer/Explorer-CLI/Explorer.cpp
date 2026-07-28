// Include the library
#include <windows.h>
#include <iostream>
#include <string>
#include <sstream>
#include <shellapi.h>

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
        std::wcout << L">>>";
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