/*
 * srvctl.h
 * 
 * Copyright © 2005, 2006  Sami Tolvanen <sami@ngim.org>
 */

#ifndef SRVCTL_H
#define SRVCTL_H 1

#include <common.h>
#include <apr_file_info.h>
#include <ngim/base.h>

/* Program names for error reporting */
#define PROGRAM_LIMITER			"limiter"
#define PROGRAM_MONITOR			"monitor"
#define PROGRAM_SCANNER			"scanner"
#define PROGRAM_SRVCTL			"srvctl"
#define PROGRAM_TAICONV			"taiconv"
#define PROGRAM_TAINLOG			"tainlog"

/* Service directory structure:
 *   DIR_BASE						<-- parameter for scanner and srvctl
 *       DIR_ACTIVE					<-- working directory for scanner
 *           ../DIR_ALL/name		<-- symbolic links for active services
 *       DIR_ALL					<-- directories for all services
 *           service				<-- parameter for monitor and tainlog, and
 *               DIR_MONITOR			working directory for monitor
 *                   FILE_LOCK
 *                   FILE_STATUS
 *                   FILE_UP
 *                   PIPE_CONTROL
 *                   PIPE_STDIN		<-- stdin for FILE_RUN
 *               DIR_TAINLOG		<-- working directory and parameter for
 *                   FILE_CURRENT	    tainlog
 *                   @...			<-- log files archived by tainlog
 *               FILE_RUN			<-- started by monitor
 *               FILE_LOG			<-- started by monitor
 *               FILE_PRIORITY		<-- read by srvctl
 */

/* File and directory names */
#define DIR_BASE				"/services"	/* The default base */
#define DIR_ACTIVE				"active"
#define DIR_ALL					"all"
#define DIR_MONITOR 			"monitor"
#define FILE_LOCK 				DIR_MONITOR "/lock"
#define FILE_STATUS 			DIR_MONITOR "/status"
#define FILE_UP 				DIR_MONITOR "/up"
#define PIPE_CONTROL 			DIR_MONITOR "/control"
#define PIPE_STDIN				DIR_MONITOR "/stdin"
#define DIR_TAINLOG				"tainlog"
#define FILE_CURRENT			"current"
#define FILE_LOG				"log"
#define FILE_RUN				"run"
#define FILE_PRIORITY			"priority"

/* Default file and directory permissions */
/* drwxr-xr-x */
#define FPROT_DIR_ACTIVE \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE |\
	 APR_FPROT_GREAD | APR_FPROT_GEXECUTE |\
	 APR_FPROT_WREAD | APR_FPROT_WEXECUTE)
/* drwxr-x--- */
#define FPROT_DIR_MONITOR \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE |\
	 APR_FPROT_GREAD | APR_FPROT_GEXECUTE)
/* -rw------- */
#define FPROT_FILE_LOCK \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE)
/* -rw-r----- */
#define FPROT_FILE_STATUS \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE |\
	 APR_FPROT_GREAD)
/* -rw------- */
#define FPROT_FILE_UP \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE)
/* -rw------- */
#define FPROT_PIPE_CONTROL \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE)
/* -rw------- */
#define FPROT_PIPE_STDIN \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE)
/* drwxr-x--- */
#define FPROT_DIR_TAINLOG \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE | APR_FPROT_UEXECUTE |\
	 APR_FPROT_GREAD | APR_FPROT_GEXECUTE)
/* -rw-r----- */
#define FPROT_FILE_CURRENT \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE |\
	 APR_FPROT_GREAD)
/* -rw-r----- */
#define FPROT_FILE_PRIORITY \
	(APR_FPROT_UREAD | APR_FPROT_UWRITE |\
	 APR_FPROT_GREAD)

/* Monitor control commands */
#define MONITOR_CMD_KILL		'k'
#define MONITOR_CMD_TERMINATE	'x'
#define MONITOR_CMD_WAKEUP		'w'

/* Environment variables */
#define ENV_SRVCTL_BASE			"SRVCTL_BASE"

/* Monitor status file
 *   status updated		NGIM_TAIN_PACK bytes
 *   run changed		NGIM_TAIN_PACK bytes
 *   log changed		NGIM_TAIN_PACK bytes
 *   run pid			4 bytes
 *   log pid			4 bytes
 *   flag_forward		1 byte
 */
#define MONITOR_STATUS_SIZE		(3 * NGIM_TAIN_PACK + 2 * 4 + 1)
#define MONITOR_STATUS_UPDATED	(0)
#define MONITOR_STATUS_CHG_RUN	(NGIM_TAIN_PACK)
#define MONITOR_STATUS_CHG_LOG	(MONITOR_STATUS_CHG_RUN + NGIM_TAIN_PACK)
#define MONITOR_STATUS_PID_RUN	(MONITOR_STATUS_CHG_LOG + NGIM_TAIN_PACK)
#define MONITOR_STATUS_PID_LOG	(MONITOR_STATUS_PID_RUN + 4)
#define MONITOR_STATUS_FORWARD	(MONITOR_STATUS_SIZE - 1)

#endif /* SRVCTL_H */
