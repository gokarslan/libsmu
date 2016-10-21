// Released under the terms of the BSD License
// (C) 2014-2016
//   Analog Devices, Inc.
//   Kevin Mehall <km@kevinmehall.net>
//   Ian Daniher <itdaniher@gmail.com>



// Editted by Kerim Gokarslan <kerimgokarslan@gmail.com> for reading data at different frequencies between 1-100k
#define MAX_FREQ 100000
#include "libsmu.hpp"
#include <stdlib.h>
#include <iostream>
#include <cstdint>
#include <vector>
#include <thread>
#include <string.h>
#include <libusb.h>

#ifdef WIN32
#include "getopt.h"
#else
#include <getopt.h>
#endif

using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

static void list_devices(Session* session)
{
	if (session->m_devices.empty()) {
		cerr << "smu: no supported devices plugged in" << endl;
	} else {
		for (auto dev: session->m_devices) {
			printf("%s: serial %s: fw %s: hw %s\n",
					dev->info()->label, dev->serial(),
					dev->fwver(), dev->hwver());
		}
	}
}

static void display_usage(void)
{
	printf("smu: utility for managing M1K devices\n"
		"\n"
		" -h, --help                   print this help message and exit\n"
		" -l, --list                   list supported devices currently attached to the system\n"
		" -p, --hotplug                simple session device hotplug testing\n"
		" -s, --stream                 stream samples to stdout from a single attached device\n"
		" -d, --display-calibration    display calibration data from all attached devices\n"
		" -r, --reset-calibration      reset calibration data to the defaults on all attached devices\n"
		" -w, --write-calibration <cal file> write calibration data to a single attached device\n"
		" -f, --flash <firmware image> flash firmware image to a single attached device\n");
}

static void stream_samples(Session* session, int freq, int duration, char* file_name)
{
	static int time_counter = 0;
	FILE* f_out = fopen(file_name, "w");
	auto dev = *(session->m_devices.begin());
	auto dev_info = dev->info();
	printf("The scan will start immediately with following parameters\nFrequency: %d\nDuration(sec): %d\nOutput File: %s\n", freq, duration, file_name);
	for (unsigned ch_i=0; ch_i < dev_info->channel_count; ch_i++) {
		auto ch_info = dev->channel_info(ch_i);
		dev->set_mode(ch_i, DISABLED);
        static int counter = 0;
		for (unsigned sig_i=0; sig_i < ch_info->signal_count; sig_i++) {
			auto sig = dev->signal(ch_i, sig_i);
			auto sig_info = sig->info();
			sig->measure_callback([=](float d){
                if(strcmp(ch_info->label, "A") == 0 && strcmp(sig_info->label, "Voltage") == 0){
                    counter ++;
                    if(counter == MAX_FREQ/freq){
                        //fprintf(f_out, "Channel %s, %s: %f\n", ch_info->label, sig_info->label, d);
						fprintf(f_out, "%f\n",d);
                        counter = 0;
						time_counter++;
						if(time_counter >= duration * freq){// finish it
							fclose(f_out);
							printf("The operation is finished successfully. Please check the output file.\n");
							exit(0);
							
						}
                    }
                }
			});
		}
	}
	session->configure(dev->get_default_rate());
	session->start(0);
	while ( 1 == 1 ) {session->wait_for_completion();};
}

int write_calibration(Session* session, const char *file)
{
	int ret;
	auto dev = *(session->m_devices.begin());
	ret = dev->write_calibration(file);
	if (ret <= 0) {
		if (ret == -EINVAL)
			cerr << "smu: invalid calibration data format" << endl;
		else if (ret == LIBUSB_ERROR_PIPE)
			cerr << "smu: firmware version doesn't support calibration (update to 2.06 or later)" << endl;
		else
			perror("smu: failed to write calibration data");
		return 1;
	}
	return 0;
}

void display_calibration(Session* session)
{
	vector<vector<float>> cal;
	for (auto dev: session->m_devices) {
		printf("%s: serial %s: fw %s: hw %s\n",
			dev->info()->label, dev->serial(),
			dev->fwver(), dev->hwver());
		dev->calibration(&cal);
		for (int i = 0; i < 8; i++) {
			switch (i) {
				case 0: printf("  Channel A, measure V\n"); break;
				case 1: printf("  Channel A, measure I\n"); break;
				case 2: printf("  Channel A, source V\n"); break;
				case 3: printf("  Channel A, source I\n"); break;
				case 4: printf("  Channel B, measure V\n"); break;
				case 5: printf("  Channel B, measure I\n"); break;
				case 6: printf("  Channel B, source V\n"); break;
				case 7: printf("  Channel B, source I\n"); break;
			}
			printf("    offset: %.4f\n", cal[i][0]);
			printf("    p gain: %.4f\n", cal[i][1]);
			printf("    n gain: %.4f\n", cal[i][2]);
		}
		printf("\n");
	}
}

int reset_calibration(Session* session)
{
	int ret;
	for (auto dev: session->m_devices) {
		ret = dev->write_calibration(NULL);
		if (ret <= 0) {
			if (ret == LIBUSB_ERROR_PIPE)
				cerr << "smu: firmware version doesn't support calibration (update to 2.06 or later)" << endl;
			else
				perror("smu: failed to reset calibration data");
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	int opt;
	int option_index = 0;

	// display usage info if no arguments are specified
	if (argc == 1) {
		display_usage();
		return EXIT_FAILURE;
	}

	Session* session = new Session();
	// add all available devices to the session at startup
	if (session->update_available_devices()) {
		cerr << "error initializing session" << endl;
		return 1;
	}
	for (auto dev: session->m_available_devices) {
		session->add_device(&*dev);
	}

	session->m_completion_callback = [=](unsigned status){};
	session->m_progress_callback = [=](uint64_t n){};

	// map long options to short ones
	static struct option long_options[] = {
		{"help",     0, 0, 'a'},
		{"hotplug",  0, 0, 'p'},
		{"list",     0, 0, 'l'},
		{"stream",   0, 0, 's'},
		{"display-calibration", 0, 0, 'd'},
		{"reset-calibration", 0, 0, 'r'},
		{"write-calibration", 1, 0, 'w'},
		{"flash", 1, 0, 'f'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "hplsdrw:f:",
			long_options, &option_index)) != -1) {
		switch (opt) {
			case 'p':
				session->m_hotplug_detach_callback = [=](Device* device){
					session->cancel();
					session->remove_device(device);
					printf("removed device: %s: serial %s: fw %s: hw %s\n",
							device->info()->label, device->serial(),
							device->fwver(), device->hwver());
				};

				session->m_hotplug_attach_callback = [=](Device* device){
					if (session->add_device(device))
						printf("added device: %s: serial %s: fw %s: hw %s\n",
							device->info()->label, device->serial(),
							device->fwver(), device->hwver());
				};

				// wait around doing nothing (hotplug testing)
				while (1)
					std::this_thread::sleep_for(std::chrono::seconds(10));
				break;
			case 'l':
				// list attached device info
				list_devices(session);
				break;
			case 's':
				// stream samples from an attached device to stdout
				if (!session->m_devices.empty()){
					if(argv[2] && argv[3] && argv[4]){
						stream_samples(session, strtol(argv[2], NULL, 10),  strtol(argv[3], NULL, 10), argv[4]);
						
					}else{
						cerr << "smu: bad arguments given. (use smu -s freq durationsec file)" << endl;
						return EXIT_FAILURE;
						
					}
					
				}  else {
					cerr << "smu: no supported devices plugged in" << endl;
					return EXIT_FAILURE;
				}
				break;
			case 'd':
				// display calibration data from all attached m1k devices
				display_calibration(session);
				break;
			case 'r':
				// reset calibration data of all attached m1k devices
				if (session->m_devices.empty()) {
					cerr << "smu: no supported devices plugged in" << endl;
					return EXIT_FAILURE;
				}
				if (reset_calibration(session)) {
					return EXIT_FAILURE;
				}
				cout << "smu: successfully reset calibration data" << endl;
				break;
			case 'w':
				// write calibration data to a single attached m1k device
				if (session->m_devices.empty()) {
					cerr << "smu: no supported devices plugged in" << endl;
					return EXIT_FAILURE;
				} else if (session->m_devices.size() > 1) {
					cerr << "smu: multiple devices attached, calibration only works on a single device" << endl;
					cerr << "Please detach all devices except the one targeted for calibration." << endl;
					return EXIT_FAILURE;
				}
				if (write_calibration(session, optarg))
					return EXIT_FAILURE;
				cout << "smu: successfully updated calibration data" << endl;
				break;
			case 'f':
				// flash firmware image to an attached m1k device
				try {
					session->flash_firmware(optarg);
				} catch (const std::exception& e) {
					cout << "smu: failed updating firmware: " << e.what() << endl;
					return EXIT_FAILURE;
				}
				cout << "smu: successfully updated firmware" << endl;
				cout << "Please unplug and replug the device to finish the process." << endl;
				break;
			case 'h':
				display_usage();
				break;
			default:
				display_usage();
				return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
