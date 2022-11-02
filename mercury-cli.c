/*
 *      Mercury power meter command line data fetching utility.
 *
 * 	Implementation note:
 * 	Exclusive access to the power meter implemented using semaphore (MERCURY_SEMAPHORE)
 * 	so that multiple utilites can get data simultaneously without conflicts. Please make
 * 	sure all users have proper rights to the semaphore e.g.
 *
 * 	$ ls -l /dev/shm/sem.MERCURY_RS485
 * 	-rw-rw-rw- 1 root root 16 Mar 26 23:52 /dev/shm/sem.MERCURY_RS485
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>

#include <semaphore.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include "mercury236.h"

#define OPT_DEBUG "--debug"
#define OPT_HELP "--help"
#define OPT_TEST_RUN "--testRun"
#define OPT_TEST_FAIL "--testFail"
#define OPT_HUMAN "--human"
#define OPT_CSV "--csv"
#define OPT_JSON "--json"
#define OPT_HEADER "--header"

#define BSZ 255

// int debugPrint = 0;

// command-line options
static struct option long_options[] = {
	{"addr", 1, 0, 'i'},
	{"port", 1, 0, 'p'},
	{"debug", 0, 0, 'd'},
	{"help", 0, 0, 'h'},
	{"testRun", 0, 0, 1},
	{"testFail", 0, 0, 2},
	{"human", 0, 0, 3},
	{"csv", 0, 0, 4},
	{"json", 0, 0, 5},
	{"header", 0, 0, 6},
};

typedef enum
{
	EXIT_OK = 0,
	EXIT_FAIL = 1
} ExitCode;

typedef enum // Output formatting
{
	OF_HUMAN = 0, // human readable
	OF_CSV = 1,	  // comma-separated values
	OF_JSON = 2	  // json
} OutputFormat;

void getDateTimeStr(char *str, int length, time_t time)
{
	struct tm *ti = localtime(&time);

	snprintf(str, length, "%4d-%02d-%02d %02d:%02d:%02d",
			 ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
			 ti->tm_hour, ti->tm_min, ti->tm_sec);
}

// -- Command line usage help
void printUsage()
{
	printf("Usage: mercury RS485 [OPTIONS] ...\n\r\n\r");
	printf("  RS485\t\taddress of RS485 dongle (e.g. /dev/ttyUSB0), required\n\r");
	printf("  %s\tto print extra debug info\n\r", OPT_DEBUG);
	printf("  %s\tdry run to see output sample, as if the mains was ON\n\r", OPT_TEST_RUN);
	printf("  %s\tdry run to get output sample, as if the mains was OFF\n\r", OPT_TEST_FAIL);
	printf("\n\r");
	printf("  Output formatting:\n\r");
	printf("  %s\thuman readable (default)\n\r", OPT_HUMAN);
	printf("  %s\t\tCSV\n\r", OPT_CSV);
	printf("  %s\tjson\n\r", OPT_JSON);
	printf("  %s\tto print data header (with %s only)\n\r", OPT_HEADER, OPT_CSV);
	printf("\n\r");
	printf("  %s\tprints this screen\n\r", OPT_HELP);
}

// -- Output formatting and print
void printOutput(int format, OutputBlock o, int header)
{
	// getting current time for timestamp
	char timeStamp[BSZ];
	getDateTimeStr(timeStamp, BSZ, time(NULL));

	switch (format)
	{
	case OF_HUMAN:
		printf("  Mains status:                         %8s\n\r", (o.ms) ? "On" : "Off");
		printf("  Voltage (V):             		%8.2f %8.2f %8.2f\n\r", o.U.p1, o.U.p2, o.U.p3);
		printf("  Current (A):             		%8.2f %8.2f %8.2f\n\r", o.I.p1, o.I.p2, o.I.p3);
		printf("  Cos(f):                  		%8.2f %8.2f %8.2f (%8.2f)\n\r", o.C.p1, o.C.p2, o.C.p3, o.C.sum);
		printf("  Frequency (Hz):          		%8.2f\n\r", o.f);
		printf("  Phase angles (deg):      		%8.2f %8.2f %8.2f\n\r", o.A.p1, o.A.p2, o.A.p3);
		printf("  Active power (W):        		%8.2f %8.2f %8.2f (%8.2f)\n\r", o.P.p1, o.P.p2, o.P.p3, o.P.sum);
		printf("  Reactive power (VA):     		%8.2f %8.2f %8.2f (%8.2f)\n\r", o.S.p1, o.S.p2, o.S.p3, o.S.sum);
		printf("  Total consumed, all tariffs (KW):	%8.2f\n\r", o.PR.ap);
		printf("    including day tariff (KW):		%8.2f\n\r", o.PRT[0].ap);
		printf("    including night tariff (KW):	%8.2f\n\r", o.PRT[1].ap);
		printf("  Yesterday consumed (KW): 		%8.2f\n\r", o.PY.ap);
		printf("  Today consumed (KW):     		%8.2f\n\r", o.PT.ap);
		break;

	case OF_CSV:
		if (header)
		{
			// to be the same order as params below
			printf("DT,U1,U2,U3,I1,I2,I3,P1,P2,P2,Psum,S1,S2,S3,Ssum,C1,C2,C3,Csum,F,A1,A2,A3,PRa,PYa,PTa,MS\n\r");
		}
		printf("%s,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n\r",
			   timeStamp,
			   o.U.p1, o.U.p2, o.U.p3,
			   o.I.p1, o.I.p2, o.I.p3,
			   o.P.p1, o.P.p2, o.P.p3, o.P.sum,
			   o.S.p1, o.S.p2, o.S.p3, o.S.sum,
			   o.C.p1, o.C.p2, o.C.p3, o.C.sum,
			   o.f,
			   o.A.p1, o.A.p2, o.A.p3,
			   o.PR.ap, o.PRT[0].ap, o.PRT[1].ap,
			   o.PY.ap,
			   o.PT.ap,
			   o.ms);
		break;

	case OF_JSON:
		printf("{\"mainsStatus\":%d,\"U\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f},\"I\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f},\"CosF\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f,\"sum\":%.2f},\"F\":%.2f,\"A\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f},\"P\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f,\"sum\":%.2f},\"S\":{\"p1\":%.2f,\"p2\":%.2f,\"p3\":%.2f,\"sum\":%.2f},\"PR\":{\"ap\":%.2f},\"PR-day\":{\"ap\":%.2f},\"PR-night\":{\"ap\":%.2f},\"PY\":{\"ap\":%.2f},\"PT\":{\"ap\":%.2f}}\n\r",
			   o.ms,
			   o.U.p1, o.U.p2, o.U.p3,
			   o.I.p1, o.I.p2, o.I.p3,
			   o.C.p1, o.C.p2, o.C.p3, o.C.sum,
			   o.f,
			   o.A.p1, o.A.p2, o.A.p3,
			   o.P.p1, o.P.p2, o.P.p3, o.P.sum,
			   o.S.p1, o.S.p2, o.S.p3, o.S.sum,
			   o.PR.ap, o.PRT[0].ap, o.PRT[1].ap,
			   o.PY.ap,
			   o.PT.ap);
		break;

	default:
		printf("Invalid formatting.\n\r");
		exit(EXIT_FAIL);
	}
}

int main(int argc, char *args[])
{
	// must have RS485 address (1st required param)
	if (argc < 2)
	{
		printf("Error: no RS485 device specified\n\r\n\r");
		printUsage();
		exit(EXIT_FAIL);
	}

	// get command line options
	int dryRun = 0, dryFail = 0, format = OF_HUMAN, header = 0;

	char dev[BSZ];
	strncpy(dev, args[1], BSZ);

	int opt;
	int option_index = 0;

	char *addr;
	char *port;

	while ((opt = getopt_long(argc, args, "i:p:dh", long_options, &option_index)) != -1)
	{
		switch (opt)
		{
		case 'd':
			break;
		case 1:
			dryRun = 1;
			break;
		case 2:
			dryFail = 1;
			break;
		case 3:
			format = OF_HUMAN;
			break;
		case 4:
			format = OF_CSV;
			break;
		case 5:
			format = OF_JSON;
			break;
		case 6:
			header = 1;
			break;
		case 'i':
			addr = optarg;
			break;
		case 'p':
			port = optarg;
			break;
		case 'h':
		case '?':
			printUsage();
			exit(EXIT_OK);
			break;
		default:
			printUsage();
			exit(EXIT_FAILURE);
		}
	}

	if (!(addr && port))
	{
		printf("Error: must specify ip address and port\n\r");
		exit(EXIT_FAILURE);
	}

	if (dryRun && dryFail)
	{
		printf("Error: use either %s or %s command line option.\n\r", OPT_TEST_RUN, OPT_TEST_FAIL);
		exit(EXIT_FAIL);
	}

	OutputBlock o;
	bzero(&o, sizeof(o));

	if (dryRun)
		o.ms = MS_ON;
	if (dryFail)
		o.ms = MS_OFF;

	int exitCode = OK;

	if (!dryRun && !dryFail)
	{

		int pm_socket;
		struct sockaddr_in pm_address;
		pm_address.sin_family = AF_INET;
		pm_address.sin_port = htons(atoi(port));
		inet_pton(AF_INET, addr, &pm_address.sin_addr.s_addr);

		pm_socket = socket(AF_INET, SOCK_STREAM, 0);
		int con_status = connect(pm_socket, (struct sockaddr *)&pm_address, sizeof(pm_address));

		if (con_status == -1)
		{
			printf("ERROR: Couldn't establish connection\n\r");
			exit(EXIT_FAILURE);
		}

		// char *buffer = "3b 01 02 02 02 02 02 02 02 97 36";

		// while (1)
		// {
		// 	// int len = send(pm_socket, buffer, sizeof(buffer), 0);
		// 	// printf("Sending...\n\r");
		// 	// printf("len: %d\n\r", len);
		// 	// char buf[255];
		// 	// int len2 = recv(pm_socket, &buf, sizeof(buf), 0);
		// 	// if (len2 > 0)
		// 	// {
		// 	// 	printf("Recieved: %s\n\r", buf);
		// 	// 	printf("len2: %d\n\r", len2);
		// 	// }
		// 	checkChannel(pm_socket);

		// 	sleep(1);
		// }

		// close(pm_socket);

		switch (checkChannel(pm_socket))
		{
		case OK:
			// Seems that power is on
			o.ms = MS_ON;

			if (OK != initConnection(pm_socket))
				goto stop_conversation;

			// Get voltage by phases
			if (OK != getU(pm_socket, &o.U))
				goto stop_conversation;

			// Get current by phases
			if (OK != getI(pm_socket, &o.I))
				goto stop_conversation;

			// Get last avg voltage
			if (OK != getLastAvgU(pm_socket, &o.LastAvgU))
				goto stop_conversation;

			// Get power cos(f) by phases
			if (OK != getCosF(pm_socket, &o.C))
				goto stop_conversation;

			// Get grid frequency
			if (OK != getF(pm_socket, &o.f))
				goto stop_conversation;

			// Get phase angles
			if (OK != getA(pm_socket, &o.A))
				goto stop_conversation;

			// Get active power consumption by phases
			if (OK != getP(pm_socket, &o.P))
				goto stop_conversation;

			// Get reactive power consumption by phases
			if (OK != getS(pm_socket, &o.S))
				goto stop_conversation;

			// Get power counter from reset, for yesterday and today
			if (
				OK != getW(pm_socket, &o.PR, PP_RESET, 0, 0) ||			// total from reset
				OK != getW(pm_socket, &o.PRT[0], PP_RESET, 0, 0 + 1) || // day tariff from reset
				OK != getW(pm_socket, &o.PRT[1], PP_RESET, 0, 1 + 1) || // night tariff from reset
				OK != getW(pm_socket, &o.PY, PP_YESTERDAY, 0, 0) ||
				OK != getW(pm_socket, &o.PT, PP_TODAY, 0, 0))
				goto stop_conversation;

		stop_conversation:
			closeConnection(pm_socket);
			close(pm_socket);
			exitCode = OK;
			break;

		case CHECK_CHANNEL_FAILURE:
			close(pm_socket);
			// assume that we are here because mains power supply is off
			// which caused power meter comm channel time out.
			o.ms = MS_OFF;
			exitCode = OK;
			break;

		default:
			close(pm_socket);
			// assume that we are here because mains power supply is off
			o.ms = MS_OFF;
			exitCode = OK;
			break;
		}
	}

	// print the results
	printOutput(format, o, header);

	exit(exitCode);
}
