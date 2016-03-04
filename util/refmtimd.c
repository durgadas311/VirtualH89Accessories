#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

int sectab[32] = {0};
int secflg[32] = {0};

struct geom {
	int spt;
	int ssz;
	int lat;
};

struct format {
	int media;
	int dens;
	int ntrk;
	int nsid;
	int trklen;
	int interlace;
	struct geom geom;
};

int log_format(int fd, struct format *fmt, unsigned char *buf) {
	int tot = fmt->nsid * fmt->ntrk * fmt->geom.spt * fmt->geom.ssz;
	write(fd, buf, tot);

	memset(buf, 0, 128);
	sprintf(buf, "%dm%dz%dp%ds%dt%dd%di%dl\n", fmt->media,
		fmt->geom.ssz, fmt->geom.spt, fmt->nsid, fmt->ntrk,
		fmt->dens, fmt->interlace, fmt->geom.lat);
	write(fd, buf, 128);
	return 0;
}

void get_track_imd(struct format *fmt, unsigned char **bp, unsigned char **tp) {
	unsigned char *b = *bp;
	unsigned char *t = tp != NULL ? *tp : NULL;
	int dens = (*b++ >= 3);
	int trk = *b++;
	int sid = *b++;
	int cylmap = (sid & 0x80);
	int hedmap = (sid & 0x40);
	sid &= 1;
	int spt = *b++;
	int ssz = (128 << (*b++ & 0x03));
	if (fmt->geom.spt == 0) {
		fmt->dens = dens;
		fmt->geom.spt = spt;
		fmt->geom.ssz = ssz;
		int x;
		for (x = 0; x < fmt->geom.spt; ++x) {
			sectab[x] = *b++;
		}
		fmt->geom.lat = sectab[1] - sectab[0];
	} else {
		if (fmt->geom.spt != spt || fmt->geom.ssz != ssz ||
				fmt->dens != dens) {
			fprintf(stderr, "SPT/SSZ mismatch in IMD file\n");
			exit(1);
		}
		b += fmt->geom.spt;
	}
	if (cylmap) {
		b += fmt->geom.spt;
	}
	if (hedmap) {
		b += fmt->geom.spt;
	}
	if (t == NULL) {
		if (fmt->ntrk > 0 && trk != fmt->ntrk) {
			fprintf(stderr, "Warning: track %d out of sequence\n", trk);
		}
		if (trk >= fmt->ntrk) fmt->ntrk = trk + 1;
		if (sid >= fmt->nsid) fmt->nsid = sid + 1;
	}
	int x;
	for (x = 0; x < spt; ++x) {
		int type = *b++;
		--type;
		if (type > 1) {
			fprintf(stderr, "Unsupported sector type %d in IMD file\n", type);
			exit(1);
		}
		int sec = sectab[x];
		int off = (sec - 1) * ssz;
		if (type == 0) {
			if (t != NULL) memcpy(t + off, b, ssz);
			b += ssz;
		} else {
			if (t != NULL) memset(t + off, *b, ssz);
			++b;
		}
	}
	*bp = b;
	if (t != NULL) {
		t += spt * ssz;
		*tp = t;
	}
}

char *read_imd(char *infile, struct format *fmt, unsigned char *buf, int len) {
	unsigned char *b = buf;
	while (b - buf < len && *b != 0x1a) ++b;
	if (b - buf >= len) {
		fprintf(stderr, "No header terminator - bad IMD file\n");
		exit(1);
	}
	++b;
	unsigned char *bb = b;
	// first scan file for errors and get geometry...
	do {
		get_track_imd(fmt, &b, NULL);
	} while (b - buf < len);
	unsigned char *buft = malloc(fmt->ntrk * fmt->nsid *
					fmt->geom.spt * fmt->geom.ssz);
	if (buft == NULL) {
		perror("malloc");
		exit(1);
	}
	b = bb;
	unsigned char *t = buft;
	do {
		get_track_imd(fmt, &b, &t);
	} while (b - buf < len);

	free(buf);
	return buft;
}

struct td0_track {
	unsigned char spt;
	unsigned char ptrk;
	unsigned char psid;
	unsigned char crc;
} __attribute__ ((packed));

struct td0_sector {
	unsigned char trk;
	unsigned char sid;
	unsigned char sec;
	unsigned char sln;
	unsigned char syn;
	unsigned char crc;
	unsigned short len;
} __attribute__ ((packed));

struct td0_data_2_0 {
	unsigned char len;
	unsigned char byt[];
} __attribute__ ((packed));

struct td0_data_2_1 {
	unsigned char len_2;	// length / 2, i.e. length in "words".
	unsigned short fil;
} __attribute__ ((packed));

struct td0_data_2 {
	unsigned char typ;	// 0 or 1
	union {
		struct td0_data_2_0 d0;
		struct td0_data_2_1 d1;
	};
} __attribute__ ((packed));

#if 0
struct td0_data_0 {
	//unsigned short len;
	unsigned char byt[];
} __attribute__ ((packed));
#endif

struct td0_data_1 {
	unsigned short len_2;	// length / 2, i.e. length in "words".
	unsigned short fil;
} __attribute__ ((packed));

struct td0_data {
	unsigned char typ;
	union {
		unsigned char d0; // struct td0_data_0 d0;
		struct td0_data_1 d1;
		struct td0_data_2 d2;
	};
} __attribute__ ((packed));

unsigned char *fbuf, *fbuft;

void get_track_td0(struct format *fmt, unsigned char **bp, unsigned char **tp) {
	unsigned char *b = *bp;
	unsigned char *t = tp != NULL ? *tp : NULL;
	struct td0_track *tk = (struct td0_track *)b;
	b += sizeof(*tk);
	if (tk->spt == 255) { // EOF
		*bp = b;
		if (t != NULL) {
			*tp = t;
		}
		return;
	}
	struct td0_sector *sc = (struct td0_sector *)b;

	int spt = tk->spt;
	int ssz = (128 << (sc->sln & 0x03));
	int trk = sc->trk;
	int sid = sc->sid;
	int sec = sc->sec;
	if (fmt->geom.spt == 0) {
		fmt->geom.spt = spt;
		fmt->geom.ssz = ssz;
		fmt->geom.lat = 1;
	} else {
		if (fmt->geom.spt != spt || fmt->geom.ssz != ssz) {
			fprintf(stderr, "SPT/SSZ mismatch in TD0 file trk %d (%d,%d):(%d,%d)\n",
				trk, spt,ssz,fmt->geom.spt,fmt->geom.ssz);
			exit(1);
		}
	}
	if (t == NULL) {
		if (fmt->ntrk > 0 && trk != fmt->ntrk) {
			fprintf(stderr, "Warning: track %d out of sequence\n", trk);
		}
		if (trk >= fmt->ntrk) fmt->ntrk = trk + 1;
		if (sid >= fmt->nsid) fmt->nsid = sid + 1;
	}
	int x;
	unsigned char *st = t;
	for (x = 0; x < spt; ++x) {
		sc = (struct td0_sector *)b;
		b += sizeof(*sc);
		if (t == NULL) {
			sectab[x] = sc->sec;
			if (x == 1) {
				fmt->geom.lat = sectab[1] - sectab[0];
			}
			b += sc->len;
			continue;
		}
		int off = (sc->sec - 1) * ssz;
		t = st + off;
		struct td0_data *td = (struct td0_data *)b;
//printf("Track %d sector %d type %d @ %d [%d]\n", trk, sc->sec, td->typ,b - fbuf, t - fbuft);
		switch(td->typ) {
		case 2: {
unsigned char *sb = b;
++b;
while (b - sb < sc->len) {
	struct td0_data_2 *td2 = (struct td0_data_2 *)b;
			switch(td2->typ) {
			case 0:
				memcpy(t, &td2->d0.byt[0], td2->d0.len);
				t += td2->d0.len;
				b += td2->d0.len + 2;
				break;
			case 1: {
				int i;
				unsigned short *tw = (unsigned short *)t;
				for (i = 0; i < td2->d1.len_2; ++i) {
					*tw++ = td2->d1.fil;
				}
				t += td2->d1.len_2 * 2;
				b += 4;
				} break;
			default:
				fprintf(stderr, "Format error type 2: %02x @ %d\n", td2->typ, b - fbuf);
				exit(1);
			}
}
			continue;
			} break;
		case 0:
			//memcpy(t, &td->d0.byt[0], td->d0.len);
			//t += td->d0.len;
			memcpy(t, &td->d0, ssz);
			t += ssz;
			b += sc->len;
			break;
		case 1: {
			int i;
			unsigned short *tw = (unsigned short *)t;
			for (i = 0; i < td->d1.len_2; ++i) {
				*tw++ = td->d1.fil;
			}
			t += td->d1.len_2 * 2;
			b += sc->len;
			} break;
		default:
			fprintf(stderr, "Format error type 0: %02x @ %d\n", td->typ, b - fbuf);
			exit(1);
		}
	}
	*bp = b;
	if (t != NULL) {
		*tp = st + spt * ssz;
	}
}

struct td0_header {
	unsigned char unk1[4];
	unsigned char vers;
	unsigned char dens;
	unsigned char dtype;
	unsigned char tdens;
	unsigned char dos;
	unsigned char surf;
	unsigned short crc;
} __attribute__ ((packed));

struct td0_header2 {
	unsigned short crc;
	unsigned short len;
	unsigned char ymd[3];
	unsigned char hms[3];
} __attribute__ ((packed));

char *read_td0(char *infile, struct format *fmt, unsigned char *buf, int len) {
	if (strcmp(buf, "td") == 0) {
		fprintf(stderr, "Advance compression TD0 not supported\n");
		exit(1);
	}
	struct td0_header *h1 = (struct td0_header *)buf;
	struct td0_header2 *h2 = (struct td0_header2 *)(h1 + 1);
	char *label = (char *)(h2 + 1);
	if (h2->len > 0) {
		printf("%.*s\n", h2->len, label);
	}
	printf("Created %02d/%02d/%04d %02d:%02d:%02d\n",
		h2->ymd[1], h2->ymd[2], h2->ymd[0] + 1900,
		h2->hms[0], h2->hms[1], h2->hms[2]);
	fmt->media = h1->dtype == 2 ? 5 : 8;	// guess
	fmt->dens = (h1->tdens & 1) != 0;	// guess
	unsigned char *b = (unsigned char *)(label + h2->len);
	unsigned char *bb = b;
	// first scan file for errors and get geometry...
	do {
		get_track_td0(fmt, &b, NULL);
	} while (b - buf < len);
	int buftlen = fmt->ntrk * fmt->nsid * fmt->geom.spt * fmt->geom.ssz;
	unsigned char *buft = malloc(buftlen);
	if (buft == NULL) {
		perror("malloc");
		exit(1);
	}
	memset(buft, 0, buftlen);
	b = bb;
	unsigned char *t = buft;
	fbuft = buft;
	do {
		get_track_td0(fmt, &b, &t);
	} while (b - buf < len);
	free(buf);
	return buft;
}

int main(int argc, char **argv) {
	int fd;
	unsigned char *buf;
	int c;
	int err = 0;
	// defaults:
	struct format fmt = { 0 };

	extern char *optarg;
	extern int optind;

/*
	while ((c = getopt(argc, argv, "r:R")) != EOF) {
		switch(c) {
		case 'r':
			infile = optarg;
			break;
		case 'R':
			++raw;
			break;
		default:
			break;
		}
	}
*/

	if (argc - optind != 2) {
		fprintf(stderr, "Usage: %s <IMD-image> <new-image>\n"
			, argv[0]);
		exit(1);
	}
	char *imd = argv[optind];
	char *name = argv[optind + 1];

	struct stat stb;
	fd = open(imd, O_RDONLY);
	if (fd < 0) {
		perror(imd);
		exit(1);
	}
	fstat(fd, &stb);
	buf = malloc(stb.st_size);
	if (buf == NULL) {
		perror("malloc");
		close(fd);
		exit(1);
	}
	int x = read(fd, buf, stb.st_size);
	close(fd);
	if (x != stb.st_size) {
		perror(imd);
		exit(1);
	}

	fbuf = buf;
	if (strcasecmp(buf, "TD") == 0) {
		buf = read_td0(imd, &fmt, buf, stb.st_size);
	} else if (strncmp(buf, "IMD ", 4) == 0) {
		buf = read_imd(imd, &fmt, buf, stb.st_size);
	} else {
		fprintf(stderr, "Unrecognized input file type\n");
		exit(1);
	}
	if (fmt.ntrk == 77) {
		fmt.media = 8;
	} else if (fmt.ntrk == 40 || fmt.ntrk == 80) {
		fmt.media = 5;
	} else {
		fprintf(stderr, "Error: wrong number of tracks: %d\n", fmt.ntrk);
		exit(1);
	}

	fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (fd < 0) {
		perror(name);
		exit(1);
	}

	err = log_format(fd, &fmt, buf);
	close(fd);
	free(buf);
	return err;
}
