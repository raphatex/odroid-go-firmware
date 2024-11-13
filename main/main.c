#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
// #include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_flash_partitions.h"
#include "rom/crc.h"

#include <string.h>

#include "odroid_sdcard.h"
#include "odroid_display.h"
#include "input.h"

#include "../components/ugui/ugui.h"


const char* SD_CARD = "/sd";
//const char* HEADER = "ODROIDGO_FIRMWARE_V00_00";
const char* HEADER_V00_01 = "ODROIDGO_FIRMWARE_V00_01";

#define FIRMWARE_DESCRIPTION_SIZE (40)
char FirmwareDescription[FIRMWARE_DESCRIPTION_SIZE];

// <partition type=0x00 subtype=0x00 label='name' flags=0x00000000 length=0x00000000>
// 	<data length=0x00000000>
// 		[...]
// 	</data>
// </partition>
typedef struct
{
    uint8_t type;
    uint8_t subtype;
    uint8_t _reserved0;
    uint8_t _reserved1;

    uint8_t label[16];

    uint32_t flags;
    uint32_t length;
} odroid_partition_t;

// ------

uint16_t fb[320 * 240];
UG_GUI gui;
char tempstring[512];

#define ITEM_COUNT (4)
char** files;
int fileCount;
const char* path = "/sd/odroid/firmware";
char* VERSION = NULL;

#define TILE_WIDTH (86)
#define TILE_HEIGHT (48)
#define TILE_LENGTH (TILE_WIDTH * TILE_HEIGHT * 2)
//uint8_t TileData[TILE_LENGTH];


static void pset(UG_S16 x, UG_S16 y, UG_COLOR color)
{
    fb[y * 320 + x] = color;
}

static void ui_update_display()
{
    ili9341_write_frame_rectangleLE(0, 0, 320, 240, fb);
}

static void ui_draw_image(short x, short y, short width, short height, uint16_t* data)
{
    for (short i = 0 ; i < height; ++i)
    {
        for (short j = 0; j < width; ++j)
        {
            uint16_t pixel = data[i * width + j];
            UG_DrawPixel(x + j, y + i, pixel);
        }
    }
}

// TODO: default bad image tile
void ui_firmware_image_get(const char* filename, uint16_t* outData)
{
    //printf("%s: filename='%s'\n", __func__, filename);
    const uint8_t DEFAULT_DATA = 0xff;

    FILE* file = fopen(filename, "rb");
    if (!file)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
        return;
    }

    // Check the header
    const size_t headerLength = strlen(HEADER_V00_01);
    char* header = malloc(headerLength + 1);
    if(!header)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
        goto ui_firmware_image_get_exit;
    }

    // null terminate
    memset(header, 0, headerLength + 1);

    size_t count = fread(header, 1, headerLength, file);
    if (count != headerLength)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
        goto ui_firmware_image_get_exit;
    }

    if (strncmp(HEADER_V00_01, header, headerLength) != 0)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
        goto ui_firmware_image_get_exit;
    }

    //printf("Header OK: '%s'\n", header);

    // read description
    count = fread(FirmwareDescription, 1, FIRMWARE_DESCRIPTION_SIZE, file);
    if (count != FIRMWARE_DESCRIPTION_SIZE)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
        goto ui_firmware_image_get_exit;
    }

    // read tile
    count = fread(outData, 1, TILE_LENGTH, file);
    if (count != TILE_LENGTH)
    {
        memset(outData, DEFAULT_DATA, TILE_LENGTH);
    }


ui_firmware_image_get_exit:
    free(header);
    fclose(file);
}


static void UpdateDisplay()
{
    ui_update_display();
}

static void DisplayError(const char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (240 / 2) - (12 / 2);
    UG_SetForecolor(C_RED);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayMessage(const char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (240 / 2) + 8 + (12 / 2) + 16;
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayProgress(int percent)
{
    if (percent > 100) percent = 100;

    const int WIDTH = 200;
    const int HEIGHT = 12;
    const int FILL_WIDTH = WIDTH * (percent / 100.0f);

    short left = (320 / 2) - (WIDTH / 2);
    short top = (240 / 2) - (HEIGHT / 2) + 16;
    UG_FillFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_WHITE);
    UG_DrawFrame(left - 1, top - 1, left + WIDTH + 1, top + HEIGHT + 1, C_BLACK);

    if (FILL_WIDTH > 0)
    {
        UG_FillFrame(left, top, left + FILL_WIDTH, top + HEIGHT, C_GREEN);
    }

    //UpdateDisplay();
}

static void DisplayFooter(const char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = 240 - (16 * 2) - 8;
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}

static void DisplayHeader(const char* message)
{
    UG_FontSelect(&FONT_8X12);
    short left = (320 / 2) - (strlen(message) * 9 / 2);
    short top = (16 + 8);
    UG_SetForecolor(C_BLACK);
    UG_SetBackcolor(C_WHITE);
    UG_FillFrame(0, top, 319, top + 12, C_WHITE);
    UG_PutString(left, top, message);

    UpdateDisplay();
}


//---------------
void boot_application()
{
    printf("Booting application.\n");

    // Set firmware active
    const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (partition == NULL)
    {
        DisplayError("NO BOOT PART ERROR");
    }

    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK)
    {
        DisplayError("BOOT SET ERROR");
    }

    // reboot
    esp_restart();
}


#define ESP_PARTITION_TABLE_OFFSET CONFIG_PARTITION_TABLE_OFFSET /* Offset of partition table. Backwards-compatible name.*/
#define ESP_PARTITION_TABLE_MAX_LEN 0xC00 /* Maximum length of partition table data */
#define ESP_PARTITION_TABLE_MAX_ENTRIES (ESP_PARTITION_TABLE_MAX_LEN / sizeof(esp_partition_info_t)) /* Maximum length of partition table data, including terminating entry */

#define PART_TYPE_APP 0x00
#define PART_SUBTYPE_FACTORY 0x00

static void write_partition_table(odroid_partition_t* parts, size_t parts_count)
{
    esp_err_t err;


    // Read table
    const esp_partition_info_t* partition_data = (const esp_partition_info_t*)malloc(ESP_PARTITION_TABLE_MAX_LEN);
    if (!partition_data)
    {
        DisplayError("TABLE MEMORY ERROR");
    }

    err = esp_flash_read(NULL, (void*)partition_data, ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (err != ESP_OK)
    {
        DisplayError("TABLE READ ERROR");
    }

    // Find end of first partitioned
    int startTableEntry = -1;
    size_t startFlashAddress = 0xffffffff;

    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        const esp_partition_info_t *part = &partition_data[i];
        if (part->magic == 0xffff) break;

        if (part->magic == ESP_PARTITION_MAGIC)
        {
            if (part->type == PART_TYPE_APP &&
                part->subtype == PART_SUBTYPE_FACTORY)
            {
                startTableEntry = i + 1;
                startFlashAddress = part->pos.offset + part->pos.size;
                break;
            }
        }
    }

    if (startTableEntry < 0)
    {
        DisplayError("NO FACTORY PARTITION ERROR");
    }

    printf("%s: startTableEntry=%d, startFlashAddress=%#08x\n",
        __func__, startTableEntry, startFlashAddress);

    // blank partition table entries
    for (int i = startTableEntry; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        memset(&partition_data[i], 0xff, sizeof(esp_partition_info_t));
    }

    // Add partitions
    size_t offset = 0;
    for (int i = 0; i < parts_count; ++i)
    {
        esp_partition_info_t* part = &partition_data[startTableEntry + i];
        part->magic = ESP_PARTITION_MAGIC;
        part->type = parts[i].type;
        part->subtype = parts[i].subtype;
        part->pos.offset = startFlashAddress + offset;
        part->pos.size = parts[i].length;
        for (int j = 0; j < 16; ++j)
        {
            part->label[j] = parts[i].label[j];
        }
        part->flags = parts[i].flags;

        offset += parts[i].length;
    }

    //abort();

    // Erase partition table
    if (ESP_PARTITION_TABLE_MAX_LEN > 4096)
    {
        DisplayError("TABLE SIZE ERROR");
    }

    err = esp_flash_erase_region(NULL, ESP_PARTITION_TABLE_OFFSET, 4096);
    if (err != ESP_OK)
    {
        DisplayError("TABLE ERASE ERROR");
    }

    // Write new table
    err = esp_flash_write(NULL, (void*)partition_data, ESP_PARTITION_TABLE_OFFSET, ESP_PARTITION_TABLE_MAX_LEN);
    if (err != ESP_OK)
    {
        DisplayError("TABLE WRITE ERROR");
    }

    esp_partition_unload_all();
}


static void ui_draw_title();

//uint8_t tileData[TILE_LENGTH];

void flash_firmware(const char* fullPath)
{
    size_t count;

    printf("%s: HEAP=%#010lx\n", __func__, esp_get_free_heap_size());

    ui_draw_title();
    ui_update_display();


    printf("Opening file '%s'.\n", fullPath);

    FILE* file = fopen(fullPath, "rb");
    if (file == NULL)
    {
        DisplayError("NO FILE ERROR");
    }

    // Check the header
    const size_t headerLength = strlen(HEADER_V00_01);
    char* header = malloc(headerLength + 1);
    if(!header)
    {
        DisplayError("MEMORY ERROR");
    }

    // null terminate
    memset(header, 0, headerLength + 1);

    count = fread(header, 1, headerLength, file);
    if (count != headerLength)
    {
        DisplayError("HEADER READ ERROR");
    }

    if (strncmp(HEADER_V00_01, header, headerLength) != 0)
    {
        DisplayError("HEADER MATCH ERROR");
    }

    printf("Header OK: '%s'\n", header);
    free(header);

    // read description
    count = fread(FirmwareDescription, 1, FIRMWARE_DESCRIPTION_SIZE, file);
    if (count != FIRMWARE_DESCRIPTION_SIZE)
    {
        DisplayError("DESCRIPTION READ ERROR");
    }

    // ensure null terminated
    FirmwareDescription[FIRMWARE_DESCRIPTION_SIZE - 1] = 0;

    printf("FirmwareDescription='%s'\n", FirmwareDescription);
    DisplayHeader(FirmwareDescription);
    //UpdateDisplay();

    // Tile
    uint16_t* tileData = malloc(TILE_LENGTH);
    if (!tileData)
    {
        DisplayError("TILE MEMORY ERROR");
    }

    count = fread(tileData, 1, TILE_LENGTH, file);
    if (count != TILE_LENGTH)
    {
        DisplayError("TILE READ ERROR");
    }

    const uint16_t tileLeft = (320 / 2) - (TILE_WIDTH / 2);
    const uint16_t tileTop = (16 + 16 + 16);
    ui_draw_image(tileLeft, tileTop,
        TILE_WIDTH, TILE_HEIGHT, tileData);

    free(tileData);

    // Tile border
    UG_DrawFrame(tileLeft - 1, tileTop - 1, tileLeft + TILE_WIDTH, tileTop + TILE_HEIGHT, C_BLACK);
    UpdateDisplay();

    // start to begin, b back
    DisplayMessage("[START]");
    DisplayFooter("[B] Cancel");
    //UpdateDisplay();

    odroid_gamepad_state previousState;
    input_read(&previousState);
    while (true)
    {
        odroid_gamepad_state state;
        input_read(&state);

        if(!previousState.values[ODROID_INPUT_START] && state.values[ODROID_INPUT_START])
        {
            break;
        }
        else if(!previousState.values[ODROID_INPUT_B] && state.values[ODROID_INPUT_B])
        {
            fclose(file);
            return;
        }

        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    DisplayMessage("");
    DisplayFooter("");
    //UpdateDisplay();


    DisplayMessage("Verifying ...");


    const int ERASE_BLOCK_SIZE = 4096;
    void* data = malloc(ERASE_BLOCK_SIZE);
    if (!data)
    {
        DisplayError("DATA MEMORY ERROR");
    }


    // Verify file integerity
    size_t current_position = ftell(file);


    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);


    uint32_t expected_checksum;
    fseek(file, file_size - sizeof(expected_checksum), SEEK_SET);
    count = fread(&expected_checksum, 1, sizeof(expected_checksum), file);
    if (count != sizeof(expected_checksum))
    {
        DisplayError("CHECKSUM READ ERROR");
    }
    printf("%s: expected_checksum=%#010lx\n", __func__, expected_checksum);


    fseek(file, 0, SEEK_SET);

    uint32_t checksum = 0;
    size_t check_offset = 0;
    while(true)
    {
        count = fread(data, 1, ERASE_BLOCK_SIZE, file);
        if (check_offset + count == file_size)
        {
            count -= 4;
        }

        checksum = crc32_le(checksum, data, count);
        check_offset += count;

        if (count < ERASE_BLOCK_SIZE) break;
    }

    printf("%s: checksum=%#010lx\n", __func__, checksum);

    if (checksum != expected_checksum)
    {
        DisplayError("CHECKSUM MISMATCH ERROR");
    }

    // restore location to end of description
    fseek(file, current_position, SEEK_SET);

    //while(1) vTaskDelay(1);


    // Determine start address from end of 'factory' partition
    const esp_partition_t* factory_part = esp_partition_find_first(ESP_PARTITION_TYPE_APP,
            ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    if (factory_part == NULL)
    {
         printf("esp_partition_find_first failed. (FACTORY)\n");

         DisplayError("FACTORY PARTITION ERROR");
    }

    const size_t FLASH_START_ADDRESS = factory_part->address + factory_part->size;
    printf("%s: FLASH_START_ADDRESS=%#010x\n", __func__, FLASH_START_ADDRESS);


    const size_t PARTS_MAX = 20;
    int parts_count = 0;
    odroid_partition_t* parts = malloc(sizeof(odroid_partition_t) * PARTS_MAX);
    if (!parts)
    {
        DisplayError("PARTITION MEMORY ERROR");
    }

    // Copy the firmware
    size_t curren_flash_address = FLASH_START_ADDRESS;

    while(true)
    {
        // printf("curren_flash_address:0x%x\n", curren_flash_address);
        if (ftell(file) >= (file_size - sizeof(checksum)))
        {
            break;
        }

        // Partition
        odroid_partition_t slot;
        count = fread(&slot, 1, sizeof(slot), file);
        if (count != sizeof(slot))
        {
            DisplayError("PARTITION READ ERROR");
        }

        if (parts_count >= PARTS_MAX)
        {
            DisplayError("PARTITION COUNT ERROR");
        }

        if (slot.type == 0xff)
        {
            DisplayError("PARTITION TYPE ERROR");
        }

        if (curren_flash_address + slot.length > 16 * 1024 * 1024)
        {
            DisplayError("PARTITION LENGTH ERROR");
        }

        if ((curren_flash_address & 0xffff0000) != curren_flash_address)
        {
            DisplayError("PARTITION LENGTH ALIGNMENT ERROR");
        }


        // Data Length
        uint32_t length;
        count = fread(&length, 1, sizeof(length), file);
        strncpy(tempstring, (char*)slot.label, 16);
        printf("slot:type(%d),subtype(%d),label(%s),length(%d),data_size(%d)\n", slot.type, slot.subtype, tempstring, (int)slot.length, (int)length);
        if (count != sizeof(length))
        {
            DisplayError("LENGTH READ ERROR");
        }

        if (length > slot.length)
        {
            printf("%s: data length error - length=%lx, slot.length=%lx\n",
                __func__, length, slot.length);

            DisplayError("DATA LENGTH ERROR");
        }

        size_t nextEntry = ftell(file) + length;

        if (length > 0)
        {
            // erase
            int eraseBlocks = length / ERASE_BLOCK_SIZE;
            if (eraseBlocks * ERASE_BLOCK_SIZE < length) ++eraseBlocks;

            // Display
            sprintf(tempstring, "Erasing ... (%d)", parts_count);

            printf("%s\n", tempstring);
            DisplayProgress(0);
            DisplayMessage(tempstring);

            esp_err_t ret = esp_flash_erase_region(NULL, curren_flash_address, eraseBlocks * ERASE_BLOCK_SIZE);
            if (ret != ESP_OK)
            {
                printf("esp_flash_erase_region failed. eraseBlocks=%d(0x%x)\n", eraseBlocks, ret);
                DisplayError("ERASE ERROR");
            }


            // Write data
            int totalCount = 0;
            for (int offset = 0; offset < length; offset += ERASE_BLOCK_SIZE)
            {
                // Display
                sprintf(tempstring, "Writing %s (%d%%)", (char*)slot.label, (int)(100*offset/length));

                printf("%s - %#08x\n", tempstring, offset);
                DisplayProgress((float)offset / (float)(length - ERASE_BLOCK_SIZE) * 100.0f);
                DisplayMessage(tempstring);

                // read
                //printf("Reading offset=0x%x\n", offset);
                count = fread(data, 1, ERASE_BLOCK_SIZE, file);
                if (count <= 0)
                {
                    DisplayError("DATA READ ERROR");
                }

                if (offset + count >= length)
                {
                    count = length - offset;
                }


                // flash
                //printf("Writing offset=0x%x\n", offset);
                //ret = esp_partition_write(part, offset, data, count);
                ret = esp_flash_write(NULL, data, curren_flash_address + offset, count);
                if (ret != ESP_OK)
        		{
        			printf("esp_flash_write failed. address=%#08x\n", curren_flash_address + offset);
                    DisplayError("WRITE ERROR");
        		}

                totalCount += count;
            }

            if (totalCount != length)
            {
                printf("Size mismatch: lenght=%#08lx, totalCount=%#08x\n", length, totalCount);
                DisplayError("DATA SIZE ERROR");
            }


            // TODO: verify
            // esp_flash_



            // Notify OK
            sprintf(tempstring, "OK: [%d] Length=%#08lx", parts_count, length);

            printf("%s\n", tempstring);
            //DisplayFooter(tempstring);
        }

        parts[parts_count++] = slot;
        curren_flash_address += slot.length;


        // Seek to next entry
        if (fseek(file, nextEntry, SEEK_SET) != 0)
        {
            DisplayError("SEEK ERROR");
        }

    }

    fclose(file);


    // Utility
    FILE* util = fopen("/sd/odroid/firmware/utility.bin", "rb");
    if (util)
    {
        if ((curren_flash_address & 0xffff0000) != curren_flash_address)
        {
            DisplayError("ALIGNMENT ERROR");
        }


        // Get file size
        fseek(util, 0, SEEK_END);
        size_t length = ftell(util);
        fseek(util, 0, SEEK_SET);

        printf("utility.bin - length=%d\n", length);


        // TODO: Determine if there is room

        // Erase
        int eraseBlocks = length / ERASE_BLOCK_SIZE;
        if (eraseBlocks * ERASE_BLOCK_SIZE < length) ++eraseBlocks;

        // Display
        sprintf(tempstring, "Erasing Utility ...");

        printf("%s\n", tempstring);
        DisplayProgress(0);
        DisplayMessage(tempstring);

        esp_err_t ret = esp_flash_erase_region(NULL, curren_flash_address, eraseBlocks * ERASE_BLOCK_SIZE);
        if (ret != ESP_OK)
        {
            printf("esp_flash_erase_region failed. eraseBlocks=%d\n", eraseBlocks);
            DisplayError("ERASE ERROR");
        }


        // Write data
        int totalCount = 0;
        for (int offset = 0; offset < length; offset += ERASE_BLOCK_SIZE)
        {
            // Display
            sprintf(tempstring, "Writing Utility");

            printf("%s - %#08x\n", tempstring, offset);
            DisplayProgress((float)offset / (float)(length - ERASE_BLOCK_SIZE) * 100.0f);
            DisplayMessage(tempstring);

            // read
            //printf("Reading offset=0x%x\n", offset);
            count = fread(data, 1, ERASE_BLOCK_SIZE, util);
            if (count <= 0)
            {
                DisplayError("DATA READ ERROR");
            }

            if (offset + count >= length)
            {
                count = length - offset;
            }


            // flash
            //printf("Writing offset=0x%x\n", offset);
            //ret = esp_partition_write(part, offset, data, count);
            ret = esp_flash_write(NULL, data, curren_flash_address + offset, count);
            if (ret != ESP_OK)
            {
                printf("esp_flash_write failed. address=%#08x\n", curren_flash_address + offset);
                DisplayError("WRITE ERROR");
            }

            totalCount += count;
        }

        // Add partition
        odroid_partition_t util_part;
        memset(&util_part, 0, sizeof(util_part));


        util_part.type = PART_TYPE_APP;
        util_part.subtype = PART_SUBTYPE_TEST;

        strcpy((char*)util_part.label, "utility");

        util_part.flags = 0;

        // 64k align
        if ((length & 0xffff0000) != length) length += 0x10000;
        util_part.length = length & 0xffff0000;


        parts[parts_count++] = util_part;

        fclose(util);
    }


    // Write partition table
    write_partition_table(parts, parts_count);


    free(data);

    // Close SD card
    odroid_sdcard_close();

    // clear framebuffer
    ili9341_clear(0x0000);

    // boot firmware
    boot_application();
}



static void ui_draw_title()
{
    const char* TITLE = "ODROID-GO";

    UG_FillFrame(0, 0, 319, 239, C_WHITE);

    // Header
    UG_FillFrame(0, 0, 319, 15, C_MIDNIGHT_BLUE);
    UG_FontSelect(&FONT_8X8);
    const short titleLeft = (320 / 2) - (strlen(TITLE) * 9 / 2);
    UG_SetForecolor(C_WHITE);
    UG_SetBackcolor(C_MIDNIGHT_BLUE);
    UG_PutString(titleLeft, 4, TITLE);

    // Footer
    UG_FillFrame(0, 239 - 16, 319, 239, C_MIDNIGHT_BLUE);
    const short footerLeft = (320 / 2) - (strlen(VERSION) * 9 / 2);
    UG_SetForecolor(C_DARK_GRAY);
    UG_PutString(footerLeft, 240 - 4 - 8, VERSION);
}

static void ui_draw_page(char** files, int fileCount, int currentItem)
{
    printf("%s: HEAP=%#010lx\n", __func__, esp_get_free_heap_size());

    int page = currentItem / ITEM_COUNT;
    page *= ITEM_COUNT;

    ui_draw_title();

    const int innerHeight = 240 - (16 * 2); // 208
    const int itemHeight = innerHeight / ITEM_COUNT; // 52

    const int rightWidth = (213); // 320 * (2.0 / 3.0)
    const int leftWidth = 320 - rightWidth;

    // Tile width = 86, height = 48 (16:9)
    const short imageLeft = (leftWidth / 2) - (86 / 2);
    const short textLeft = 320 - rightWidth;


	if (fileCount < 1)
	{
		// const char* text = "(none)";
        //
        // uint16_t id = TXB_ID_0 + (ITEM_COUNT / 2);
        // UG_TextboxSetText(&window1, id, (char*)text);

        ui_update_display();
	}
	else
	{
        uint16_t* tile = malloc(TILE_LENGTH);
        if (!tile) abort();

        char* displayStrings[ITEM_COUNT];
        for(int i = 0; i < ITEM_COUNT; ++i)
        {
            displayStrings[i] = NULL;
        }

	    for (int line = 0; line < ITEM_COUNT; ++line)
	    {
			if (page + line >= fileCount) break;

            //uint16_t id = TXB_ID_0 + line;
            short top = 16 + (line * itemHeight) - 1;

	        if ((page) + line == currentItem)
	        {
                UG_SetForecolor(C_BLACK);
                UG_SetBackcolor(C_YELLOW);
                UG_FillFrame(0, top + 2, 319, top + itemHeight - 1 - 1, C_YELLOW);
	        }
	        else
	        {
                UG_SetForecolor(C_BLACK);
                UG_SetBackcolor(C_WHITE);
                UG_FillFrame(0, top + 2, 319, top + itemHeight - 1 - 1, C_WHITE);
	        }

			char* fileName = files[page + line];
			if (!fileName) abort();

			displayStrings[line] = (char*)malloc(strlen(fileName) + 1);
            strcpy(displayStrings[line], fileName);
            displayStrings[line][strlen(fileName) - 3] = 0; // ".fw" = 3


            size_t fullPathLength = strlen(path) + 1 + strlen(fileName) + 1;
            char* fullPath = (char*)malloc(fullPathLength);
            if (!fullPath) abort();

            strcpy(fullPath, path);
            strcat(fullPath, "/");
            strcat(fullPath, fileName);
            ui_firmware_image_get(fullPath, tile);
            ui_draw_image(imageLeft, top + 2, TILE_WIDTH, TILE_HEIGHT, tile);

            free(fullPath);

            // Tile border
            //UG_DrawFrame(imageLeft - 1, top + 1, imageLeft + TILE_WIDTH, top + 2 + TILE_HEIGHT, C_BLACK);

	        //UG_TextboxSetText(&window1, id, displayStrings[line]);
            UG_FontSelect(&FONT_8X12);
            UG_PutString(textLeft, top + 2 + 2 + 16, displayStrings[line]);
	    }

        ui_update_display();

        for(int i = 0; i < ITEM_COUNT; ++i)
        {
            free(displayStrings[i]);
        }

        free(tile);
	}
}

const char* ui_choose_file(const char* path)
{
    const char* result = NULL;

    printf("%s: HEAP=%#010lx\n", __func__, esp_get_free_heap_size());

    files = 0;
    fileCount = odroid_sdcard_files_get(path, ".fw", &files);
    printf("%s: fileCount=%d\n", __func__, fileCount);

    // At least one firmware must be available
    if (fileCount < 1)
    {
        DisplayError("NO FILES ERROR");
    }


    // Selection
    int currentItem = 0;
    ui_draw_page(files, fileCount, currentItem);

    odroid_gamepad_state previousState;
    input_read(&previousState);

    while (true)
    {
		odroid_gamepad_state state;
		input_read(&state);

        int page = currentItem / ITEM_COUNT;
        page *= ITEM_COUNT;

		if (fileCount > 0)
		{
	        if(!previousState.values[ODROID_INPUT_DOWN] && state.values[ODROID_INPUT_DOWN])
	        {
	            if (fileCount > 0)
				{
					if (currentItem + 1 < fileCount)
		            {
		                ++currentItem;
		                ui_draw_page(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = 0;
		                ui_draw_page(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_UP] && state.values[ODROID_INPUT_UP])
	        {
	            if (fileCount > 0)
				{
					if (currentItem > 0)
		            {
		                --currentItem;
		                ui_draw_page(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = fileCount - 1;
						ui_draw_page(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_RIGHT] && state.values[ODROID_INPUT_RIGHT])
	        {
	            if (fileCount > 0)
				{
					if (page + ITEM_COUNT < fileCount)
		            {
		                currentItem = page + ITEM_COUNT;
		                ui_draw_page(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = 0;
						ui_draw_page(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_LEFT] && state.values[ODROID_INPUT_LEFT])
	        {
	            if (fileCount > 0)
				{
					if (page - ITEM_COUNT >= 0)
		            {
		                currentItem = page - ITEM_COUNT;
		                ui_draw_page(files, fileCount, currentItem);
		            }
					else
					{
						currentItem = page;
						while (currentItem + ITEM_COUNT < fileCount)
						{
							currentItem += ITEM_COUNT;
						}

		                ui_draw_page(files, fileCount, currentItem);
					}
				}
	        }
	        else if(!previousState.values[ODROID_INPUT_A] && state.values[ODROID_INPUT_A])
	        {
	            size_t fullPathLength = strlen(path) + 1 + strlen(files[currentItem]) + 1;

	            char* fullPath = (char*)malloc(fullPathLength);
	            if (!fullPath) abort();

	            strcpy(fullPath, path);
	            strcat(fullPath, "/");
	            strcat(fullPath, files[currentItem]);

	            result = fullPath;
                break;
	        }
            else if (!previousState.values[ODROID_INPUT_MENU] && state.values[ODROID_INPUT_MENU])
            {
                ui_draw_title();
                DisplayMessage("Exiting ...");
                UpdateDisplay();

                boot_application();

                // should not reach
                abort();
            }
		}

        previousState = state;
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    odroid_sdcard_files_free(files, fileCount);

    return result;
}

static void menu_main()
{
    sprintf(tempstring,"Ver: %s-%s", COMPILEDATE, GITREV);

    ui_draw_title();

    // Check SD card
    esp_err_t ret = odroid_sdcard_open(SD_CARD);
    if (ret != ESP_OK)
    {
        DisplayError("SD CARD ERROR");
    }

    esp_flash_init(NULL);

    // Check for /odroid/firmware

    while(1)
    {
        const char* fileName = ui_choose_file(path);
        if (!fileName) abort();

        printf("%s: fileName='%s'\n", __func__, fileName);

        flash_firmware(fileName);

        free(fileName);
    }
}


void app_main(void)
{
    const char* VER_PREFIX = "Ver: ";
    size_t ver_size = strlen(VER_PREFIX) + strlen(COMPILEDATE) + 1 + strlen(GITREV) + 1;
    VERSION = malloc(ver_size);
    if (!VERSION) abort();
    nvs_flash_init();

    strcpy(VERSION, VER_PREFIX);
    strcat(VERSION, COMPILEDATE);
    strcat(VERSION, "-");
    strcat(VERSION, GITREV);

    printf("odroid-go-firmware (%s). HEAP=%#010lx\n", VERSION, esp_get_free_heap_size());

    input_init();


    ili9341_init();
    ili9341_clear(0xffff);

    UG_Init(&gui, pset, 320, 240);

    menu_main();


    while(1)
    {
        vTaskDelay(1);
    }
}
