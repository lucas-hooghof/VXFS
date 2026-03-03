#include "../VXFS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef enum {
    VXFS_OK = 0,
    VXFS_ERR_NO_FREE_DIR_ENTRY,
    VXFS_ERR_NO_FREE_EXTENT,
    VXFS_ERR_WRITE_INODE,
    VXFS_ERR_WRITE_DIRENTRY,
} VXFS_ERROR;

VXFS_ERROR AllocateExtentForDir(FILE* image, VXFS_SUPERBLOCK* superblock, VXFS_INODE* inode, VXFS_EXTENT* out_extent)
{
    VXFS_EXTENT extent = {0};
    bool found = false;

    for (uint32_t i = superblock->DataRegionStart; i < superblock->TotalSectors; i++)
    {
        if (!GetBitmap(image, superblock->DataBitmapStart, i))
        {
            extent.StartSector = i;
            extent.SizeInSectors = 1; 
            extent.ExtentID = inode->ExtentID;
            extent.ExtentTableID = inode->ExtentTableID;
            extent.NextExtentUsed = false;

            SetBitmap(image, superblock->DataBitmapStart, i, 1);

 
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


    newnode.InodeID       = superblock->NextInodeID;
    newnode.InodeTableID  = superblock->NextInodeTableID;
    newnode.ExtentID      = superblock->NextExtentID;
    newnode.ExtentTableID = superblock->NextExtentTableID;
    newnode.Flags         = flags;
    newnode.free          = 0;


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


    fseek(image, write_offset, SEEK_SET);
    fwrite(&direntry, 1, sizeof(direntry), image);
    fwrite(name, 1, direntry.NameLenght, image);


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

/* --- Parse flags from argv --- */
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
    return flags;
}

/* --- Split a path into parts --- */
static char** SplitPath(const char* path, size_t* count)
{
    char* copy = strdup(path);
    size_t capacity = 8, used = 0;
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

/* --- Find a directory entry in an extent --- */
static bool FindDirEntry(FILE* image, VXFS_SUPERBLOCK* superblock, VXFS_EXTENT extent, const char* name, VXFS_DIRENTRY* out_entry)
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
                if (out_entry) *out_entry = entry;
                free(tmp);
                return true;
            }

            free(tmp);
            offset += sizeof(VXFS_DIRENTRY) + entry.NameLenght;
        }
    }

    return false;
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "Usage: %s <image> <localfile> <vxfs path> [FLAGS...]\n", argv[0]);
        return 1;
    }

    const char* imagefile = argv[1];
    const char* localfile = argv[2];
    const char* vxpath    = argv[3];
    uint32_t flags        = ParseFlags(argc, argv, 4);

    FILE* image = fopen(imagefile, "rb+");
    if (!image) { perror("Opening image"); return 1; }

    FILE* infile = fopen(localfile, "rb");
    if (!infile) { perror("Opening local file"); fclose(image); return 1; }

    fseek(infile, 0, SEEK_END);
    long filesize = ftell(infile);
    fseek(infile, 0, SEEK_SET);

    VXFS_SUPERBLOCK superblock = {0};
    fread(&superblock, 1, sizeof(superblock), image);

    VXFS_INODE rootinode = {0};
    fseek(image, superblock.InodeTablesStart * BLOCK_SIZE + sizeof(VXFS_INODE), SEEK_SET);
    fread(&rootinode, 1, sizeof(rootinode), image);

    VXFS_EXTENT rootextent = {0};
    fseek(image, superblock.ExtentTableStart * BLOCK_SIZE, SEEK_SET);
    fread(&rootextent, 1, sizeof(rootextent), image);

    size_t depth = 0;
    char** parts = SplitPath(vxpath, &depth);

    VXFS_INODE current_inode = rootinode;
    VXFS_EXTENT current_extent = rootextent;
    VXFS_DIRENTRY found_entry;

    for (size_t p = 0; p < depth; p++)
    {
        bool exists = FindDirEntry(image, &superblock, current_extent, parts[p], &found_entry);
        if (!exists)
        {
            // Create directory if intermediate, else create file
            uint32_t node_flags = (p == depth-1) ? flags : DIR;
            VXFS_ERROR err = vxfs_create_node(image, &superblock, current_extent, current_inode, parts[p], node_flags);
            if (err != VXFS_OK)
            {
                fprintf(stderr, "Failed to create '%s', error %d\n", parts[p], err);
                fclose(image); fclose(infile); return 1;
            }

            // Refresh current inode/extent
            if (!FindDirEntry(image, &superblock, current_extent, parts[p], &found_entry))
            {
                fprintf(stderr, "Failed to find newly created node '%s'\n", parts[p]);
                fclose(image); fclose(infile); return 1;
            }
        }

        // Move current inode/extent
        fseek(image, superblock.InodeTablesStart * BLOCK_SIZE +
                     found_entry.InodeTableID * superblock.InodeTableSize * BLOCK_SIZE +
                     found_entry.InodeID * sizeof(VXFS_INODE), SEEK_SET);
        fread(&current_inode, 1, sizeof(current_inode), image);

        fseek(image, superblock.ExtentTableStart * BLOCK_SIZE +
                     current_inode.ExtentTableID * superblock.ExtentTableSize * BLOCK_SIZE +
                     current_inode.ExtentID * sizeof(VXFS_EXTENT), SEEK_SET);
        fread(&current_extent, 1, sizeof(current_extent), image);
    }

    // Allocate data sector for file (first free sector)
    VXFS_EXTENT file_extent = {0};
    file_extent.ExtentID = current_inode.ExtentID;
    file_extent.ExtentTableID = current_inode.ExtentTableID;
    file_extent.SizeInSectors = (filesize + BLOCK_SIZE - 1)/BLOCK_SIZE; 

    bool allocated = false;
    for (uint32_t i = superblock.DataRegionStart; i < superblock.TotalSectors; i++)
    {
        uint8_t byte;
        fseek(image, superblock.DataBitmapStart * BLOCK_SIZE + i/8, SEEK_SET);
        fread(&byte, 1, 1, image);
        if (!(byte & (1 << (i % 8))))
        {
            file_extent.StartSector = i;
            byte |= 1 << (i%8);
            fseek(image, -1, SEEK_CUR);
            fwrite(&byte, 1, 1, image);
            allocated = true;
            break;
        }
    }

    printf("File Location: %ld\n",file_extent.StartSector * BLOCK_SIZE);

    if (!allocated) { fprintf(stderr,"No free data sectors\n"); fclose(image); fclose(infile); return 1; }

    // Write file extent
    fseek(image, superblock.ExtentTableStart*BLOCK_SIZE +
                 current_inode.ExtentTableID*superblock.ExtentTableSize*BLOCK_SIZE +
                 current_inode.ExtentID*sizeof(VXFS_EXTENT), SEEK_SET);
    fwrite(&file_extent, 1, sizeof(file_extent), image);

    // Write file content
    fseek(image, file_extent.StartSector*BLOCK_SIZE, SEEK_SET);
    uint8_t* buffer = malloc(file_extent.SizeInSectors*BLOCK_SIZE);
    fread(buffer, 1, filesize, infile);
    fwrite(buffer, 1, filesize, image);
    free(buffer);

    // Save superblock
    fseek(image, 0, SEEK_SET);
    fwrite(&superblock, 1, sizeof(superblock), image);

    for (size_t i=0;i<depth;i++) free(parts[i]);
    free(parts);

    fclose(image);
    fclose(infile);

    printf("File '%s' copied to '%s'\n", localfile, vxpath);
    return 0;
}