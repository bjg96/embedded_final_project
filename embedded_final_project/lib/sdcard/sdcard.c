#include <avr/io.h>
#include <stdio.h>
#include "system.h"
#include "spi.h"
#include "sdcard.h"
#include "uart.h"
#include <util/delay.h>
#include <string.h>

unsigned long sd_sector;
unsigned short sd_pos;

uint8_t SD_state = SD_STATE_NOT_INITIALIZED;

uint8_t SD_command(uint8_t cmd, uint32_t arg, uint16_t read) {
    uint16_t i;
    unsigned char buffer[read];
    cmd |= 0x40;
    
    printf("CMD (0x%02X) ", cmd);
    
    CS_ENABLE();
    spi_rxtx(cmd);
    spi_rxtx(arg>>24);
    spi_rxtx(arg>>16);
    spi_rxtx(arg>>8);
    spi_rxtx(arg);
    spi_rxtx(0x95);
    
    for(i=0; i<read; i++)
        buffer[i] = spi_rxtx(0xFF);
        
    CS_DISABLE();
    
    print_buffer(buffer, read, 16);
    
    return buffer[1];
}

uint8_t SD_command_crc(uint8_t cmd, uint32_t arg, uint16_t read, uint8_t crc) {
    uint16_t i;
    unsigned char buffer[read];
    cmd |= 0x40;
    
    printf("CMD (0x%02X) ", cmd);
    
    CS_ENABLE();
    spi_rxtx(cmd);
    spi_rxtx(arg>>24);
    spi_rxtx(arg>>16);
    spi_rxtx(arg>>8);
    spi_rxtx(arg);
    spi_rxtx(crc);
    
    for(i=0; i<read; i++)
        buffer[i] = spi_rxtx(0xFF);
    
    CS_DISABLE();
    
    print_buffer(buffer, read, 16);
    
    return buffer[1];
}

uint8_t SD_init() {
    char i;
    
    // ]r:10
    CS_DISABLE();
    for(i=0; i<10; i++) // idle for 1 bytes / 80 clocks
        spi_rxtx(0xFF);
    
    // [0x40 0x00 0x00 0x00 0x00 0x95 r:8] until we get "1"
    
    
    for(i=0; i<10 && SD_command(SD_GO_IDLE_STATE, 0x00000000, 8) != 1; i++)
        _delay_ms(100);
    
    if(i == 10) // card did not respond to initialization
        return -1;
    SD_state = SD_STATE_IDLE;

    SD_command_crc(SD_SEND_IF_COND, 0x400001aa, 8, 0x15);    

    // CMD1 until card comes out of idle, but maximum of 10 times
    for(i=0; i<10 && SD_command(SD_SEND_OP_COND, 0x00000000, 8) != 0; i++)
        _delay_ms(100);
    
    if(i == 10) // card did not come out of idle
        return -2;
    
    sd_sector = sd_pos = 0;
    
    SD_state = SD_STATE_COMMAND_READY;
    
    return 0;
}

void SD_read(uint32_t sector, uint16_t offset, uint8_t * buffer,
             uint16_t len) {
    
    uint16_t i = 0;
    
    CS_ENABLE();
    spi_rxtx(SD_READ_SINGLE_BLOCK | 0x40);
    spi_rxtx(sector>>24); // sector >> 24
    spi_rxtx(sector>>16); // sector >> 16
    spi_rxtx(sector>>8); // sector >> 8
    spi_rxtx(sector); // sector
    spi_rxtx(0x95);
    
    for(i=0; i<10 && spi_rxtx(0xFF) != 0x00; i++) {} // wait for 0
    
    for(i=0; i<10 && spi_rxtx(0xFF) != 0xFE; i++) {} // wait for data start
    
    for(i=0; i<offset; i++) // "skip" bytes
        spi_rxtx(0xFF);
    
    for(i=0; i<len; i++) // read len bytes
        buffer[i] = spi_rxtx(0xFF);
    
    for(i+=offset; i<512; i++) // "skip" again
        spi_rxtx(0xFF);
    
    // skip checksum
    spi_rxtx(0xFF);
    spi_rxtx(0xFF);
    
    CS_DISABLE();
}

void SD_write(uint32_t sector, uint8_t * buffer) {
    /* write a single block to sd card - len should ALWAYS be 512 */
    uint16_t i = 0;
    uint16_t len = 512;
    
    CS_ENABLE();
    
    spi_rxtx(SD_WRITE_SINGLE_BLOCK | 0x40);
    spi_rxtx(sector>>24); // sector >> 24
    spi_rxtx(sector>>16); // sector >> 16
    spi_rxtx(sector>>8); // sector >> 8
    spi_rxtx(sector); // sector
    spi_rxtx(0xFF);        // CRC
    
    for(i=0; i<10 && spi_rxtx(0xFF) != 0x00; i++) {} // wait for 0
    if (i == 10){
        printf("%sDid not receive 0x0\n%s", C_RED, C_WHITE);
    }
    
    // sending data must begin with a start token of 0xFE
    for(i=0; i<10 && (spi_rxtx(0xFE) & 0x05) != 0x05; i++) {}      // response should be 0bxxx00101 format (which is 5)
    if (i == 10){
        printf("%sDid not receive 0x05\n%s", C_RED, C_WHITE);
    }
    
    for(i=0; i<len; i++) // send len bytes
        spi_rxtx(buffer[i]);
    
    // skip checksum
    spi_rxtx(0xFF);
    spi_rxtx(0xFF);

    uint8_t response = spi_rxtx(0xFF);
    
    if (response == 0xE5){
        printf("%Bytes written successfully\n%s", C_RED, C_WHITE);
    }
    
    CS_DISABLE();
}
