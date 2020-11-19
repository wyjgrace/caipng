// HTS221 : measure humidity
// LPS25H : measure temperature because it's more temperature sensitive as compared to HTS221

// PRE REQUISITE
// - sudo raspi-config - interfacing options - enable i2c
// - sudo apt install libi2c-dev
// - i2cdetect -y 1 : check if i2c connection and address

// COMPILE AND RUN
// - sudo  gcc humidity_temp.c -o humidity_temp -li2c
// - ./humidity_temp

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// for i2c
#include <linux/i2c-dev.h>
#include <i2c/smbus.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// registers
#define CTRL_REG1 0x20
#define CTRL_REG2 0x21

// lsb
#define HUMIDITY_OUT_L 0x28

// msb
#define HUMIDITY_OUT_H 0x29

// calibration registers HTS221 humidity 
#define H0_rH_x2 0x30
#define H1_rH_x2 0x31
#define H0_OUT_L 0x36
#define H0_OUT_H 0x37
#define H1_OUT_L 0x3A
#define H1_OUT_H 0x3B

// calibration registers LPS25H temperature
#define TEMP_OUT_L 0x2B
#define TEMP_OUT_H 0x2C

// see datasheet on "How to intepret pressure and temperature readings in the LPS25HB pressure sensor" page 5
#define temp_offset 42.5
#define temp_scale 480

int bin2dec(int n);
float getcputemp(void);

int main (void) {
	int i2c_file;
	char *file = "/dev/i2c-1";

	// open i2c bus
	if((i2c_file = open(file, O_RDWR))<0) {
		printf("unable to open i2c bus \n");
		exit(1);
	}

	// config i2c slave
	if(ioctl(i2c_file, I2C_SLAVE, 0x5F)<0){
		printf("unable to configure i2c slave \n");
		exit(1);
	}
	
	// clean start (power off and on device)
	i2c_smbus_write_byte_data(i2c_file, CTRL_REG1, 0x00);
	i2c_smbus_write_byte_data(i2c_file, CTRL_REG1, 0x84);
	
	// polling while loop
	while(1) {

		// read calibration registers (x-axis) (see HTS221 datasheet graph page 28)
		// unsigned (non-negative) 8 bit integer 
		uint8_t h0_OUT_LSB = i2c_smbus_read_byte_data(i2c_file, H0_OUT_L);
		uint8_t h0_OUT_MSB = i2c_smbus_read_byte_data(i2c_file, H0_OUT_H);
		uint8_t h1_OUT_LSB = i2c_smbus_read_byte_data(i2c_file, H1_OUT_L);
		uint8_t h1_OUT_MSB = i2c_smbus_read_byte_data(i2c_file, H1_OUT_H);
		
		// read calibrate registers (y-axis) (see HTS221 datasheet graph page 28)
		uint8_t h0_rH_x2 = i2c_smbus_read_byte_data(i2c_file, H0_rH_x2);
		uint8_t h1_rH_x2 = i2c_smbus_read_byte_data(i2c_file, H1_rH_x2);
		
		// make into 16 bit value (left bit shift)
		int16_t h0_OUT = h0_OUT_MSB << 8 | h0_OUT_LSB;
		int16_t h1_OUT = h1_OUT_MSB << 8 | h1_OUT_LSB;

		// output relative humidity value (%) (see HTS221 datasheet page 28)
		double h0_rH = h0_rH_x2 / 2.0;
		double h1_rH = h1_rH_x2 / 2.0;

		// calc gradient
		double gradient = (h1_rH - h0_rH) / (h1_OUT - h0_OUT);
		
		// calc y-intercept
	 	double y_intercept = h1_rH - (gradient * h1_OUT);

		// read ambient humidity 
		uint8_t humidity_LSB = i2c_smbus_read_byte_data(i2c_file, HUMIDITY_OUT_L);
		uint8_t humidity_MSB = i2c_smbus_read_byte_data(i2c_file, HUMIDITY_OUT_H);

		// make into 16 bit  value (left bit shift)
		int16_t humidity = humidity_MSB << 8 | humidity_LSB;

		// calc ambient humidity
		double H_rH = (gradient * humidity) + y_intercept;
		
		// read registers		
		uint8_t temp_OUT_LSB = i2c_smbus_read_byte_data(i2c_file, TEMP_OUT_L);
		uint8_t temp_OUT_MSB = i2c_smbus_read_byte_data(i2c_file, TEMP_OUT_H);
		uint16_t temp_OUT = temp_OUT_MSB << 8 | temp_OUT_LSB;
			
		// get temperature value 
		double temp = (temp_offset + (bin2dec(temp_OUT) / temp_scale));
		
		// get accurate temperature
		// because raw temperature is higher due to cpu temperature
		// reference github.com/initialstate/wunderground-sensehat/wiki/Part-3.-Sense-HAT-Temperature-Correction
		double accurateTemp = temp - ((getcputemp() - temp)/5.466) - 6;

		printf("Humidity: %.0f%", H_rH);
		printf("% rH \n");
		printf("Temperature: %.0f%", accurateTemp);
		printf(" celsius \n");
		
		// polling wait time
		sleep(60);
	}
}

// convert binary to decimal
int bin2dec(int n) {
	int num = n;
	int decimal = 0;
	int base = 1;
	int tempNum = num;
	while (tempNum) {
		int last_digit = tempNum % 10;
		tempNum = tempNum / 10;
		decimal += last_digit * base;
		base = base * 2;
	}
	return decimal;
}

// in case need to ref them in report haha
// from pragmaticlinux.com/2020/06/check-the-raspberry-pi-cpu-temperature
float getcputemp(void) {
	float result;
	FILE * f;
	char line[256] = "0";
	char * pos;
	result = 0.0f;
	// read file
	f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
	if (f != NULL) {
		// read first line that contains cpu temp
		fgets(line, sizeof(line)/sizeof(line[0]), f);
		fclose(f);
	}
	pos = strchr(line, '\n');
	if (pos != NULL) {
		*pos = 0;
	}
	else {
		strcpy(line, "0");
	}
	pos = line;
	while (*pos != 0) {
		if (!isdigit(*pos)) {
			strcpy(line, "0");
			break;
		}
		pos++;
	}
	result = ((float)atoi(line))/1000;
	return result;

}
