#include "../VXFS.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static uint32_t ParseFlags(int argc, char** argv, int start_index)
{
    uint32_t flags = 0;

    for (int i = start_index; i < argc; i++)
    {
        if (!strcmp(argv[i], "DIR")) flags |= DIR;
        else if (!strcmp(argv[i], "SYSTEM")) flags |= SYSTEM;
        else if (!strcmp(argv[i], "SYSLINK")) flags |= SYSLINK;
        else if (!strcmp(argv[i], "HARDLINK")) flags |= HARDLINK;
    }

    if (flags == 0)
        flags = DIR;

    return flags;
}

static char** SplitPath(const char* path, size_t* count)
{
    char* copy = strdup(path);
    size_t capacity = 8;
    size_t used = 0;

    char** parts = malloc(capacity * sizeof(char*));

    char* token = strtok(copy, "/");
    while (token)
    {
        if (used == capacity)
        {
            capacity *= 2;
            parts = realloc(parts, capacity * sizeof(char*));
        }

        parts[used++] = strdup(token);
        token = strtok(NULL, "/");
    }

    free(copy);
    *count = used;
    return parts;
}

static bool FindDir(
    FILE* image,
    VXFS_SUPERBLOCK* superblock,
    VXFS_EXTENT extent,
    const char* name,
    VXFS_DIRENTRY* out_entry
)
{
    fseek(image, extent.StartSector * BLOCK_SIZE, SEEK_SET);

    VXFS_DIRENTRY entry;

    for (uint64_t sector = 0; sector < extent.SizeInSectors; sector++)
    {
        uint64_t offset = 0;

        while (offset < BLOCK_SIZE)
        {
            long pos = ftell(image);

            if (fread(&entry, 1, sizeof(entry), image) != sizeof(entry))
                return false;

            if (!entry.valid)
                return false;

            char* tmp = malloc(entry.NameLenght + 1);
            fread(tmp, 1, entry.NameLenght, image);
            tmp[entry.NameLenght] = '\0';

            if (strcmp(tmp, name) == 0)
            {
                if (out_entry)
                    *out_entry = entry;

                free(tmp);
                return true;
            }

            free(tmp);

            offset += sizeof(VXFS_DIRENTRY) + entry.NameLenght;
        }
    }

    return false;
}

typedef enum {
    VXFS_OK = 0,
    VXFS_ERR_NO_FREE_DIR_ENTRY,
    VXFS_ERR_NO_FREE_EXTENT,
    VXFS_ERR_WRITE_INODE,
    VXFS_ERR_WRITE_DIRENTRY,
} VXFS_ERROR;

/* --- Allocate a new extent for a directory if needed --- */
VXFS_ERROR AllocateExtentForDir(FILE* image, VXFS_SUPERBLOCK* superblock, VXFS_INODE* inode, VXFS_EXTENT* out_extent)
{
    VXFS_EXTENT extent = {0};
    bool found = false;

    for (uint32_t i = superblock->DataRegionStart; i < superblock->TotalSectors; i++)
    {
        if (!GetBitmap(image, superblock->DataBitmapStart, i))
        {
            extent.StartSector = i;
            extent.SizeInSectors = 1; // start with 1 sector
            extent.ExtentID = inode->ExtentID;
            extent.ExtentTableID = inode->ExtentTableID;
            extent.NextExtentUsed = false;

            SetBitmap(image, superblock->DataBitmapStart, i, 1);

            // Write extent to extent table
            fseek(image, superblock->ExtentTableStart * BLOCK_SIZE +
                  inode->ExtentTableID * (superblock->ExtentTableSize * BLOCK_SIZE) +
                  inode->ExtentID * sizeof(VXFS_EXTENT), SEEK_SET);
            fwrite(&extent, 1, sizeof(extent), image);

            found = true;
            break;
        }
    }

    if (!found)
        return VXFS_ERR_NO_FREE_EXTENT;

    *out_extent = extent;
    return VXFS_OK;
}

/* --- Create a node, automatically allocating a directory extent if empty --- */
VXFS_ERROR vxfs_create_node(
    FILE* image,
    VXFS_SUPERBLOCK* superblock,
    VXFS_EXTENT parent_extent,
    VXFS_INODE parent_inode,
    const char* name,
    uint32_t flags
)
{
    VXFS_INODE newnode = {0};

    /* --- Allocate Inode --- */
    newnode.InodeID       = superblock->NextInodeID;
    newnode.InodeTableID  = superblock->NextInodeTableID;
    newnode.ExtentID      = superblock->NextExtentID;
    newnode.ExtentTableID = superblock->NextExtentTableID;
    newnode.Flags         = flags;
    newnode.free          = 0;

    /* Advance inode/extent counters */
    superblock->NextExtentID++;
    if (superblock->NextExtentID >= superblock->ExtentsPerTable)
    {
        superblock->NextExtentID = 0;
        superblock->NextExtentTableID++;
    }

    superblock->NextInodeID++;
    if (superblock->NextInodeID >= superblock->InodesPerTable)
    {
        superblock->NextInodeID = 0;
        superblock->NextInodeTableID++;
    }

    /* Write inode */
    uint64_t inode_offset =
        superblock->InodeTablesStart * BLOCK_SIZE +
        newnode.InodeTableID * superblock->InodeTableSize * BLOCK_SIZE +
        newnode.InodeID * sizeof(VXFS_INODE);

    fseek(image, inode_offset, SEEK_SET);
    if (fwrite(&newnode, 1, sizeof(newnode), image) != sizeof(newnode))
    {
        fprintf(stderr, "Failed to write inode for '%s'\n", name);
        return VXFS_ERR_WRITE_INODE;
    }

    /* --- Allocate extent if directory --- */
    VXFS_EXTENT dir_extent = parent_extent;
    if ((flags & DIR) && parent_extent.SizeInSectors == 0)
    {
        VXFS_ERROR err = AllocateExtentForDir(image, superblock, &newnode, &dir_extent);
        if (err != VXFS_OK)
        {
            fprintf(stderr, "Failed to allocate extent for directory '%s'\n", name);
            return err;
        }
    }

    /* --- Find free slot in parent directory --- */
    VXFS_DIRENTRY direntry = {0};
    direntry.InodeID      = newnode.InodeID;
    direntry.InodeTableID = newnode.InodeTableID;
    direntry.NameLenght   = strlen(name);
    direntry.valid        = 1;

    fseek(image, parent_extent.StartSector * BLOCK_SIZE, SEEK_SET);

    VXFS_DIRENTRY tmp;
    uint64_t write_offset = 0;
    bool found_slot = false;

    for (uint64_t sector = 0; sector < parent_extent.SizeInSectors && !found_slot; sector++)
    {
        uint64_t offset = 0;
        while (offset < BLOCK_SIZE)
        {
            long pos = ftell(image);
            if (fread(&tmp, 1, sizeof(tmp), image) != sizeof(tmp))
                return VXFS_ERR_NO_FREE_DIR_ENTRY;

            if (!tmp.valid)
            {
                write_offset = pos;
                found_slot = true;
                break;
            }

            fseek(image, tmp.NameLenght, SEEK_CUR);
            offset += sizeof(VXFS_DIRENTRY) + tmp.NameLenght;
        }
    }

    if (!found_slot)
    {
        fprintf(stderr, "No free directory entry in parent extent (StartSector: %lu, Size: %lu)\n",
                parent_extent.StartSector, parent_extent.SizeInSectors);
        return VXFS_ERR_NO_FREE_DIR_ENTRY;
    }

    /* --- Write directory entry in parent --- */
    fseek(image, write_offset, SEEK_SET);
    fwrite(&direntry, 1, sizeof(direntry), image);
    fwrite(name, 1, direntry.NameLenght, image);

    /* --- Initialize directory contents with "." and ".." --- */
    if (flags & DIR)
    {
        VXFS_DIRENTRY dot = {0};
        dot.InodeID = newnode.InodeID;
        dot.InodeTableID = newnode.InodeTableID;
        dot.NameLenght = 1;
        dot.valid = 1;

        VXFS_DIRENTRY dotdot = {0};
        dotdot.InodeID = parent_inode.InodeID;
        dotdot.InodeTableID = parent_inode.InodeTableID;
        dotdot.NameLenght = 2;
        dotdot.valid = 1;

        fseek(image, dir_extent.StartSector * BLOCK_SIZE, SEEK_SET);
        fwrite(&dot, 1, sizeof(dot), image);
        fwrite(".", 1, 1, image);

        fwrite(&dotdot, 1, sizeof(dotdot), image);
        fwrite("..", 1, 2, image);
    }

    return VXFS_OK;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <image> <path> [FLAGS...]\n", argv[0]);
        return 1;
    }

    FILE* image = fopen(argv[1], "rb+");
    if (!image)
        return 1;

    VXFS_SUPERBLOCK superblock;
    fread(&superblock, 1, sizeof(superblock), image);

    VXFS_INODE rootinode;
    fseek(image, superblock.InodeTablesStart * BLOCK_SIZE + sizeof(VXFS_INODE), SEEK_SET);
    fread(&rootinode, 1, sizeof(rootinode), image);

    VXFS_EXTENT rootextent;
    fseek(image, superblock.ExtentTableStart * BLOCK_SIZE, SEEK_SET);
    fread(&rootextent, 1, sizeof(rootextent), image);

    size_t part_count = 0;
    char** parts = SplitPath(argv[2], &part_count);

    VXFS_INODE current_inode = rootinode;
    VXFS_EXTENT current_extent = rootextent;

    for (size_t p = 0; p < part_count; p++)
    {
        VXFS_DIRENTRY found_entry;

        if (FindDir(image, &superblock, current_extent, parts[p], &found_entry))
        {
            uint64_t inode_offset =
                superblock.InodeTablesStart * BLOCK_SIZE +
                found_entry.InodeTableID * superblock.InodeTableSize * BLOCK_SIZE +
                found_entry.InodeID * sizeof(VXFS_INODE);

            fseek(image, inode_offset, SEEK_SET);
            fread(&current_inode, 1, sizeof(current_inode), image);

            uint64_t extent_offset =
                superblock.ExtentTableStart * BLOCK_SIZE +
                current_inode.ExtentTableID * superblock.ExtentTableSize * BLOCK_SIZE +
                current_inode.ExtentID * sizeof(VXFS_EXTENT);

            fseek(image, extent_offset, SEEK_SET);
            fread(&current_extent, 1, sizeof(current_extent), image);
        }
        else
        {
            uint32_t flags = (p == part_count - 1)
                ? ParseFlags(argc, argv, 3)
                : DIR;

            VXFS_ERROR err = vxfs_create_node(image, &superblock, current_extent, current_inode,parts[p], flags);
            if (err != VXFS_OK)
            {
                fprintf(stderr, "Failed to create '%s': error code %d\n", parts[p], err);
                return 1;
            }

            if (!FindDir(image, &superblock, current_extent, parts[p], &found_entry))
                return 1;

            uint64_t inode_offset =
                superblock.InodeTablesStart * BLOCK_SIZE +
                found_entry.InodeTableID * superblock.InodeTableSize * BLOCK_SIZE +
                found_entry.InodeID * sizeof(VXFS_INODE);

            fseek(image, inode_offset, SEEK_SET);
            fread(&current_inode, 1, sizeof(current_inode), image);

            uint64_t extent_offset =
                superblock.ExtentTableStart * BLOCK_SIZE +
                current_inode.ExtentTableID * superblock.ExtentTableSize * BLOCK_SIZE +
                current_inode.ExtentID * sizeof(VXFS_EXTENT);

            fseek(image, extent_offset, SEEK_SET);
            fread(&current_extent, 1, sizeof(current_extent), image);
        }
    }

    fseek(image, 0, SEEK_SET);
    fwrite(&superblock, 1, sizeof(superblock), image);

    for (size_t i = 0; i < part_count; i++)
        free(parts[i]);
    free(parts);

    fclose(image);
    return 0;
}