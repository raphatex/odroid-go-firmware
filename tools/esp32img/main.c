#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// ./esptool.py --port "/dev/ttyUSB0" --baud 921600 read_flash 0 0xf00000 imagename.bin


typedef struct {
    uint32_t offset;
    uint32_t size;
} esp_partition_pos_t;

/* Structure which describes the layout of partition table entry.
 * See docs/partition_tables.rst for more information about individual fields.
 */
typedef struct {
	uint16_t magic;
	uint8_t  type;
    uint8_t  subtype;
    esp_partition_pos_t pos;
	uint8_t  label[16];
    uint32_t flags;
} esp_partition_info_t;

//#define ESP_PARTITION_TABLE_OFFSET CONFIG_PARTITION_TABLE_OFFSET /* Offset of partition table. Backwards-compatible name.*/
#define ESP_PARTITION_TABLE_MAX_LEN 0xC00 /* Maximum length of partition table data */
#define ESP_PARTITION_TABLE_MAX_ENTRIES (ESP_PARTITION_TABLE_MAX_LEN / sizeof(esp_partition_info_t)) /* Maximum length of partition table data, including terminating entry */
#define ESP_PARTITION_TABLE_OFFSET  0x8000

#define ESP_PARTITION_MAGIC 0x50AA
#define ESP_PARTITION_MAGIC_MD5 0xEBEB


const esp_partition_info_t* partition_data;

static void load_partitions(FILE* fp)
{
    partition_data = (const esp_partition_info_t*)malloc(ESP_PARTITION_TABLE_MAX_LEN);
    if (!partition_data) abort();

    fseek(fp, ESP_PARTITION_TABLE_OFFSET, SEEK_SET);
    size_t count = fread((esp_partition_info_t*)partition_data, 1, ESP_PARTITION_TABLE_MAX_LEN, fp);
    if (count != ESP_PARTITION_TABLE_MAX_LEN) abort();
}

static void print_partitions()
{
    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        const esp_partition_info_t *part = &partition_data[i];
        if (part->magic == 0xffff ||
            part->magic == ESP_PARTITION_MAGIC_MD5)
        {
            break;
        }

        printf("partition %d:\n", i);

        printf("\tmagic=%#06x\n", part->magic);
        printf("\ttype=%#04x\n", part->type);
        printf("\tsubtype=%#04x\n", part->subtype);
        printf("\t[pos.offset=%#010x, pos.size=%#010x]\n", part->pos.offset, part->pos.size);
        printf("\tlabel='%-16s'\n", part->label);
        printf("\tflags=%#010x\n", part->flags);
        printf("\n");
    }
}

static void extract_partitions(FILE* fp)
{
    const size_t BLOCK_SIZE = 4096;


    printf("\n");

    uint32_t data_end = 0;

    for (int i = 0; i < ESP_PARTITION_TABLE_MAX_ENTRIES; ++i)
    {
        const esp_partition_info_t *part = &partition_data[i];
        if (part->magic == 0xffff ||
            part->magic == ESP_PARTITION_MAGIC_MD5)
        {
            break;
        }

        uint32_t part_end = part->pos.offset + part->pos.size;

        if (part_end > data_end) data_end = part_end;
    }

    const char* filename = "image.dat";
    printf("./esptool.py --port \"/dev/ttyUSB0\" --baud 921600 write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0 %s\n", filename);


    fseek(fp, 0, SEEK_SET);

    FILE* output = fopen(filename, "wb");
    if (!output) abort();

    uint8_t* data = malloc(data_end);
    if (!data) abort();

    size_t count = fread(data, 1, data_end, fp);
    if (count != data_end) abort();

    count = fwrite(data, 1, data_end, output);
    if (count != data_end) abort();

    fclose(output);

    free(data);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("usage: %s imagefile\n", argv[0]);
        exit(1);
    }

    const char* filename = argv[1];

    FILE* fp = fopen(filename, "rb");

    load_partitions(fp);
    print_partitions();

    extract_partitions(fp);

    fclose(fp);

    return 0;
}