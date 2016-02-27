#include <stdio.h>
#include <stdlib.h>

// Command syntax: getdiskdef <image-file> [<partition-num>]

int mediaSize_m;
int secSize_m;
int numSectors_m;
int numSides_m;
int numTracks_m;
int doubleDensity_m;
int interlaced_m;
int mediaLat_m;
int mediaCyl;
int mediaHead;
int mediaSsz;
int mediaSpt;
int mediaLat;
int result;

int checkHeader(char *buf, int n) {
    char *b = buf;
    result = 0;

    while (*b != '\n' && *b != '\0' && b - buf < n) {
        char *e;
        int p = strtoul(b, &e, 0);

        switch (tolower(*e)) {
        case 'm':
            if (p != 5 && p != 8) {
                break;
            }
            result |= 0x01;
            mediaSize_m = p;
            break;

        case 'z':
            if (p != 128 && p != 256 && p != 512 && p != 1024) {
                break;
            }
            result |= 0x02;
            secSize_m = p;
            mediaSsz = p;
            break;

        case 'p':
            if (p == 0) {
                break;
            }
            result |= 0x04;
            numSectors_m = p;
            mediaSpt = p;
            break;

        case 's':
            if (p < 1 || p > 2) {
                break;
            }
            result |= 0x08;
            numSides_m = p;
            break;

        case 't':
            if (p == 0) {
                break;
            }
            result |= 0x10;
            numTracks_m = p;
            break;

        case 'd':
            result |= 0x20;
            doubleDensity_m = (p != 0);
            break;

        case 'i':
            result |= 0x40;
            interlaced_m = (p != 0);
            break;

        case 'c':
            if (p == 0) {
                break;
            }
            result |= 0x80;
            mediaCyl = p;
            break;

        case 'h':
            if (p == 0) {
                break;
            }
            result |= 0x0100;
            mediaHead = p;
            break;

        case 'l': // optional ?
            mediaLat_m = p;
            break;

        default:
            return 0;
        }

        b = e + 1;
    }

    return (result == 0x7f || result == 0x186);
}

struct dpb {
	unsigned short spt;
	unsigned char bsh, bsm, exm;
	unsigned short dsm, drm;
	unsigned char alv0, alv1;
	unsigned short cks;
	unsigned short restrk;
	unsigned char pad[6]; // non-CP/M stuff
} __attribute__ ((packed));

struct sasi_fdisk {
	unsigned char jmp[3];
	unsigned char drive;
	unsigned char ctrl;
	unsigned char dcharistics[8];
	unsigned char assdtype[6];
	unsigned char nparts;
	unsigned char parts[9][3];
	struct dpb dpbs[8];
} __attribute__ ((packed));

void get_sasi_diskdef(void *ptab, int partno) {
	struct sasi_fdisk *fdisk = (struct sasi_fdisk *)ptab;
if (0) {
int x;
for (x = 0; x < fdisk->nparts; ++x) {
	int y = (fdisk->parts[x][0] << 16) |
			(fdisk->parts[x][1] << 8) |
			fdisk->parts[x][2];
printf("%d: %d\n", x, y);
}
for (x = 0; x < fdisk->nparts; ++x) {
printf("%d dpb: %d %d %d %d %d %d %02x%02x %d %d\n", x,
fdisk->dpbs[x].spt,
fdisk->dpbs[x].bsh, fdisk->dpbs[x].bsm, fdisk->dpbs[x].exm,
fdisk->dpbs[x].dsm, fdisk->dpbs[x].drm,
fdisk->dpbs[x].alv0, fdisk->dpbs[x].alv1,
fdisk->dpbs[x].cks,
fdisk->dpbs[x].restrk);
}
}
	if (partno >= fdisk->nparts) {
		return;
	}
	int offset = (fdisk->parts[partno][0] << 16) |
			(fdisk->parts[partno][1] << 8) |
			fdisk->parts[partno][2];
	int nextoff = (fdisk->parts[partno + 1][0] << 16) |
			(fdisk->parts[partno + 1][1] << 8) |
			fdisk->parts[partno + 1][2];
	int sxf = mediaSsz / 128;
	if (nextoff == 0) {
		nextoff = (mediaCyl * mediaHead * mediaSpt) * sxf;
	}
	printf("diskdef\n"
		"  seclen %d\n  tracks %d\n  sectrk %d\n"
		"  blocksize %d\n  maxdir %d\n  skew 0\n"
		"  boottrk %d\n  offset %d\n  os 2.2\nend\n",
		mediaSsz, (nextoff - offset) / fdisk->dpbs[partno].spt,
		fdisk->dpbs[partno].spt / sxf,
		(1 << fdisk->dpbs[partno].bsh) * 128,
		fdisk->dpbs[partno].drm + 1,
		fdisk->dpbs[partno].restrk,
		offset * 128);
}

enum fmts { MMS, Z17, M47, Z37, Z47, Z67, Z37X, Z47X, FMTMAX };
enum medias { F525, F8, MEDMAX };
enum densities { SD, DD, DENSMAX };
enum sides { SS, DS, SIDSMAX };
enum tpis { ST, DT, TPISMAX };

struct dpb floppy_dpbs[MEDMAX][FMTMAX][DENSMAX][SIDSMAX][TPISMAX] = {
[F8][M47][DD][SS][ST] = { 64, 4, 15, 0, 299, 191, 0xe0, 0x00, 48, 2 },
[F8][M47][DD][DS][ST] = { 64, 4, 15, 0, 607, 191, 0xe0, 0x00, 48, 2 },

[F525][MMS][DD][SS][ST] = { 36, 4, 15, 1,  82,  95, 0xc0, 0x00, 24, 3 },
[F525][MMS][DD][DS][ST] = { 36, 4, 15, 1, 172,  95, 0xc0, 0x00, 24, 3 },
[F525][MMS][DD][SS][DT] = { 36, 5, 31, 3,  85, 127, 0x80, 0x00, 32, 3 },
[F525][MMS][DD][DS][DT] = { 36, 5, 31, 3, 175, 127, 0x80, 0x00, 32, 3 },
[F8][MMS][DD][SS][ST]   = { 64, 4, 15, 0, 299, 191, 0xe0, 0x00, 48, 2 },
[F8][MMS][DD][DS][ST]   = { 64, 4, 15, 0, 607, 191, 0xe0, 0x00, 48, 2 },

[F525][Z37][SD][SS][ST] = { 20, 3,  7, 0,  91,  63, 0xc0, 0x00, 16, 3 },
[F525][Z37][SD][DS][ST] = { 20, 3,  7, 0, 187, 127, 0xf0, 0x00, 32, 3 },
[F525][Z37][SD][SS][DT] = { 20, 3,  7, 0, 191,  63, 0xc0, 0x00, 16, 3 },
[F525][Z37][SD][DS][DT] = { 20, 4, 15, 1, 195, 127, 0xc0, 0x00, 32, 3 },
[F525][Z37][DD][SS][ST] = { 32, 4, 15, 1, 151, 127, 0xf0, 0x00, 32, 2 },
[F525][Z37][DD][DS][ST] = { 32, 4, 15, 0, 155, 255, 0xf0, 0x00, 64, 2 },
[F525][Z37][DD][SS][DT] = { 32, 4, 15, 1, 155, 127, 0xc0, 0x00, 32, 2 },
[F525][Z37][DD][DS][DT] = { 32, 4, 15, 0, 315, 255, 0xf0, 0x00, 64, 2 },

[F525][Z37X][DD][SS][ST] = { 40, 3,  7, 0, 185, 127, 0xf0, 0x00, 32, 2 },
[F525][Z37X][DD][DS][ST] = { 40, 4, 15, 0, 194, 255, 0xf0, 0x00, 64, 2 },
[F525][Z37X][DD][SS][DT] = { 40, 4, 15, 1, 194, 127, 0xc0, 0x00, 32, 2 },
[F525][Z37X][DD][DS][DT] = { 40, 4, 15, 0, 394, 255, 0xf0, 0x00, 64, 2 },

[F8][Z47][DD][SS][ST]    = { 52, 4, 15, 0, 242, 127, 0xc0, 0x00, 32, 2 },
[F8][Z47][DD][DS][ST]    = { 52, 4, 15, 0, 493, 255, 0xf0, 0x00, 64, 2 },
[F8][Z47X][DD][SS][ST]   = { 64, 4, 15, 0, 299, 127, 0xc0, 0x00, 32, 2 },
[F8][Z47X][DD][DS][ST]   = { 64, 4, 15, 0, 607, 255, 0xf0, 0x00, 64, 2 },
};

void get_floppy_diskdef() {
	enum fmts fmt;
	enum medias media = mediaSize_m == 5 ? F525 : F8;
	enum densities dens = doubleDensity_m ? DD : SD;
	enum sides sid = numSides_m > 1 ? DS : SS;
	enum tpis tpi = mediaSize_m == 5 && numTracks_m > 40 ? DT : ST;
	if (media == F8) {
		fmt = Z47;
	} else {
		fmt = Z37;
	}
	if (doubleDensity_m && secSize_m == 512 && numSectors_m == 9) {
		fmt = MMS;
	}
	// can't tell M47 from Z47X... just use Z47X...
	// except by interlaced?
	if (media == F8 && secSize_m == 1024 && numSectors_m == 16) {
		fmt = interlaced_m ? Z47X : M47;
	}
	if (secSize_m == 1024) {
		fmt = media == F8 ? Z47X : Z37X;
	}
	struct dpb *dpb = &floppy_dpbs[media][fmt][dens][sid][tpi];

	printf("diskdef\n"
		"  seclen %d\n  tracks %d\n  sectrk %d\n"
		"  blocksize %d\n  maxdir %d\n  skew 0\n"
		"  boottrk %d\n  os 2.2\nend\n",
		mediaSsz, numTracks_m * numSides_m,
		dpb->spt / (secSize_m / 128),
		(1 << dpb->bsh) * 128,
		dpb->drm + 1,
		dpb->restrk);
}

int main(int argc, char **argv) {
	char buf[128];
	int partno = 0;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Syntax: %s <image-file> [<partition-num>]\n", argv[0]);
		exit(1);
	}
	if (argc > 2) {
		partno = strtoul(argv[2], NULL, 0);
	}

	FILE *fp = fopen(argv[1], "r");
	if (fp == NULL) {
		perror(argv[1]);
		exit(1);
	}
	fseek(fp, 0L, SEEK_END);
	long end = ftell(fp);
	fseek(fp, end - sizeof(buf), SEEK_SET);
	size_t rc = fread(buf, sizeof(buf), 1, fp);
	if (rc != 1 || !checkHeader(buf, sizeof(buf))) {
		// silently fail if not recognized?
		fprintf(stderr, "Failed to recognize image %d %04x\n", rc, result);
		fclose(fp);
		exit(1);
	}
	if (result == 0x7f) {
		// Floppy diskette...
		get_floppy_diskdef();
	} else {
		// SASI disk, possible partition table.
		rewind(fp);
		char ptab[512];
		fread(ptab, sizeof(ptab), 1, fp);
		get_sasi_diskdef(ptab, partno);
	}
	fclose(fp);
	return 0;
}
