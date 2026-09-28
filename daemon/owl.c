/*
 * OWL: an open Apple Wireless Direct Link (AWDL) implementation
 * Copyright (C) 2018  The Open Wireless Link Project
 * Copyright (C) 2018  Milan Stute
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <ev.h>

#include "log.h"
#include "core.h"

#define DEFAULT_AWDL_DEVICE "awdl0"
#define FAILED_DUMP "failed.pcap"

static void daemonize() {
	pid_t pid;
	long x;

	/* Fork off the parent process */
	pid = fork();

	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* On success: The child process becomes session leader */
	if (setsid() < 0)
		exit(EXIT_FAILURE);

	/* Catch, ignore and handle signals */
	/* TODO: Implement a working signal handler */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();

	/* An error occurred */
	if (pid < 0)
		exit(EXIT_FAILURE);

	/* Success: Let the parent terminate */
	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	/* or another appropriated directory */
	chdir("/");

	/* Close all open file descriptors */
	for (x = sysconf(_SC_OPEN_MAX); x >= 0; x--) {
		close(x);
	}

	/* Open the log file */
	openlog("owl", LOG_PID, LOG_DAEMON);
}

static void stop_loop(struct ev_loop *loop, ev_signal *handle __attribute__((unused)),
                      int revents __attribute__((unused))) {
	ev_break(loop, EVBREAK_ALL);
}

int main(int argc, char *argv[]) {
	int c;
	int daemon = 0;
	int dump = 0;
	int chan_num = 6;
	struct awdl_chan chan;
	int log_level = LOG_INFO;
	int filter_rssi = 1;
	int no_monitor_mode = 0;

	char wlan[PATH_MAX] = "";
	char host[IFNAMSIZ] = DEFAULT_AWDL_DEVICE;
	const char *services_file = "/run/owl/airdrop.tsv";
	const char *advertise = NULL;
	int election_metric = 0;
	int hop = 0;

	struct ev_loop *loop;

	struct daemon_state state;

	while ((c = getopt(argc, argv, "Dc:dvi:h:a:t:fNS:A:M:H")) != -1) {
		switch (c) {
			case 'D':
				daemon = 1;
				break;
			case 'c':
				chan_num = atoi(optarg);
				break;
			case 'd':
				dump = 1;
				break;
			case 'f':
				filter_rssi = 0;
				break;
			case 'v':
				if (log_level < LOG_TRACE)
					log_level++; /* increase log level with every occurence of option */
				break;
			case 'i':
				strcpy(wlan, optarg);
				break;
			case 'h':
				strcpy(host, optarg);
				break;
			case 'N':
				no_monitor_mode = 1;
				break;
			case 'S':
				services_file = *optarg ? optarg : NULL;
				break;
			case 'A':
				advertise = optarg;
				break;
			case 'H':
				hop = 1;
				break;
			case 'M':
				election_metric = atoi(optarg);
				break;
			case '?':
				if (optopt == 'i')
					fprintf(stderr, "Option -%c needs to specify a wireless interface.\n", optopt);
				return EXIT_FAILURE;
			default:
				abort();
		}
	}

	log_set_level(log_level);

	printf("              .oOXWMMMMWXOx:\n"
	       "         .oOOOx:'''''''''''':OOOx:\n"
	       "      oXOo'      ........      ':OXx.\n"
	       "           .oOOO''''''''''OOOo.\n"
	       "        oXOo'                'oOO:\n"
	       "             :oOOOOXXXXOOOOo:.\n"
	       "          oXO:'            ':OXo\n"
	       "              .:xOXXXXXXOx:.\n"
	       "          .xXMMMMMMMMMMMMMMMMXx.\n"
	       "  'XWWWWWWMMMMMMMMMMMMMMMMMMMMMMWWWWWWX'\n"
	       "    oWMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMWo\n"
	       "     OMMMMMMWWMMMMMMMMMMMMMMWWWMMMMMO\n"
	       "    OMMWx'      'xWMMMMWx'      'oXMMO\n"
	       "   :MW:            oMMx            'WM:\n"
	       "   XM'    .xOOo.    :o     .xOOo.    WX\n"
	       "   WX    :MMMMMX          :MMMMMX    xW\n"
	       "   XW    'WMMMMX   .xx.   'WMMMWX    XX\n"
	       "   'Wx    'xWMx'   OMMO    'xWMx'   xM'\n"
	       "    'XX:           'XX'           :XX'\n"
	       "      'xXOx:..................:xXWx'\n"
	       "         'xXMMMMMMMMMMMMMMMMMMWO'\n"
	       "\n"
	       "            Open Wireless Link\n"
	       "\n"
	       "\n");

	if (daemon)
		daemonize();

	switch (chan_num) {
		case 6:
			chan = CHAN_OPCLASS_6;
			break;
		case 44:
			chan = CHAN_OPCLASS_44;
			break;
		case 149:
			chan = CHAN_OPCLASS_149;
			break;
		default:
			log_error("Unsupported channel %d (use 6, 44, or 149)", chan_num);
			return EXIT_FAILURE;
	}

	if (!*wlan) {
		log_error("No interface specified");
		return EXIT_FAILURE;
	}

	state.io.wlan_no_monitor_mode = no_monitor_mode;

	if (awdl_init(&state, wlan, host, chan, dump ? FAILED_DUMP : 0) < 0) {
		log_error("could not initialize core");
		return EXIT_FAILURE;
	}
	state.awdl_state.filter_rssi = filter_rssi;
	state.services_file = services_file;
	if (hop) {
		awdl_chanseq_init_social(state.awdl_state.channel.sequence);
		log_info("Using social channel-hopping sequence (44/6)");
	}
	if (election_metric > 0) {
		/* election compares counter first; Macs' counters are huge, so counter wins, not metric */
		uint32_t counter = 100000000;
		state.awdl_state.election.self_metric = (uint32_t) election_metric;
		state.awdl_state.election.master_metric = (uint32_t) election_metric;
		state.awdl_state.election.self_counter = counter;
		state.awdl_state.election.master_counter = counter;
		log_info("Election metric=%d counter=%u (will become AWDL master)", election_metric, counter);
	}
	if (advertise) {
		char tmp[256];
		char *instance, *label, *port, *txt, *save = NULL;
		strncpy(tmp, advertise, sizeof(tmp) - 1);
		tmp[sizeof(tmp) - 1] = '\0';
		instance = strtok_r(tmp, ",", &save);
		label = strtok_r(NULL, ",", &save);
		port = strtok_r(NULL, ",", &save);
		txt = strtok_r(NULL, ",", &save);
		if (instance && label && port) {
			strncpy(state.awdl_state.service_instance, instance, sizeof(state.awdl_state.service_instance) - 1);
			strncpy(state.awdl_state.service_label, label, sizeof(state.awdl_state.service_label) - 1);
			if (txt)
				strncpy(state.awdl_state.service_txt, txt, sizeof(state.awdl_state.service_txt) - 1);
			state.awdl_state.service_port = (uint16_t) atoi(port);
			log_info("Advertising %s.%s._tcp.local port %u over AWDL",
			         state.awdl_state.service_instance, state.awdl_state.service_label,
			         state.awdl_state.service_port);
		} else {
			log_error("Invalid -A format; use instance,label,port[,txt]");
		}
	}

	if (state.io.wlan_ifindex)
		log_info("WLAN device: %s (addr %s)", state.io.wlan_ifname, ether_ntoa(&state.io.if_ether_addr));
	if (state.io.host_ifindex)
		log_info("Host device: %s", state.io.host_ifname);

	loop = EV_DEFAULT;

	awdl_schedule(loop, &state);

	ev_signal sigint, sigterm;
	ev_signal_init(&sigint, stop_loop, SIGINT);
	ev_signal_start(loop, &sigint);
	ev_signal_init(&sigterm, stop_loop, SIGTERM);
	ev_signal_start(loop, &sigterm);

	ev_run(loop, 0);

	awdl_free(&state);

	return EXIT_SUCCESS;
}
