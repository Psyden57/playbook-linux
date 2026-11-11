/*
 * pbmix -- minimal ALSA control utility for the PlayBook bring-up.
 *
 * There is no alsa-utils in the region-1 root, and the WM8994's playback
 * path needs its DAPM mixer switches set (they reset off).  This speaks the
 * raw ALSA control ioctls so it needs only libc.
 *
 * Usage:
 *   pbmix list [substr]          list controls (optionally name-filtered)
 *   pbmix on <substr>            set matching BOOLEAN controls to 1
 *   pbmix off <substr>           set matching BOOLEAN controls to 0
 *   pbmix set <name> <value>     set an integer/boolean control
 *   pbmix enum <name> <text>     set an enumerated control by item name
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sound/asound.h>

#define CTL_DEV "/dev/snd/controlC0"

static int fd = -1;

static const char *type_name(unsigned int t)
{
	switch (t) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN: return "bool";
	case SNDRV_CTL_ELEM_TYPE_INTEGER: return "int";
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED: return "enum";
	case SNDRV_CTL_ELEM_TYPE_BYTES: return "bytes";
	case SNDRV_CTL_ELEM_TYPE_IEC958: return "iec958";
	case SNDRV_CTL_ELEM_TYPE_INTEGER64: return "int64";
	default: return "?";
	}
}

static int get_info(const struct snd_ctl_elem_id *id,
		    struct snd_ctl_elem_info *ei)
{
	memset(ei, 0, sizeof(*ei));
	ei->id = *id;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, ei) < 0)
		return -1;
	return 0;
}

static int get_item_name(const struct snd_ctl_elem_id *id, unsigned int item,
			 char *buf, size_t n)
{
	struct snd_ctl_elem_info ei;

	memset(&ei, 0, sizeof(ei));
	ei.id = *id;
	ei.value.enumerated.item = item;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &ei) < 0)
		return -1;
	snprintf(buf, n, "%s", ei.value.enumerated.name);
	return 0;
}

static int get_value(const struct snd_ctl_elem_id *id,
		     struct snd_ctl_elem_value *ev)
{
	memset(ev, 0, sizeof(*ev));
	ev->id = *id;
	return ioctl(fd, SNDRV_CTL_IOCTL_ELEM_READ, ev);
}

static int put_value(struct snd_ctl_elem_value *ev)
{
	return ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, ev);
}

static struct snd_ctl_elem_id *list_ids(unsigned int *count)
{
	struct snd_ctl_elem_list list;
	struct snd_ctl_elem_id *ids;
	unsigned int n;

	memset(&list, 0, sizeof(list));
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0) {
		perror("ELEM_LIST");
		return NULL;
	}
	n = list.count;
	if (!n) {
		*count = 0;
		return NULL;
	}
	ids = calloc(n, sizeof(*ids));
	if (!ids)
		return NULL;
	memset(&list, 0, sizeof(list));
	list.pids = ids;
	list.space = n;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0) {
		perror("ELEM_LIST");
		free(ids);
		return NULL;
	}
	*count = list.used;
	return ids;
}

static void print_one(const struct snd_ctl_elem_id *id)
{
	struct snd_ctl_elem_info ei;
	struct snd_ctl_elem_value ev;
	unsigned int i;

	if (get_info(id, &ei) < 0)
		return;
	if (get_value(id, &ev) < 0)
		return;

	printf("%-48s %-5s n=%u [", (char *)ei.id.name,
	       type_name(ei.type), ei.count);
	for (i = 0; i < ei.count && i < 8; i++) {
		if (ei.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
			char nm[64];

			if (get_item_name(id, ev.value.enumerated.item[i],
					  nm, sizeof(nm)) == 0)
				printf("%s%s", i ? " " : "", nm);
		} else {
			printf("%s%ld", i ? " " : "", ev.value.integer.value[i]);
		}
	}
	printf("]\n");
}

static int set_control(const char *name, long value)
{
	struct snd_ctl_elem_id *ids;
	struct snd_ctl_elem_info ei;
	struct snd_ctl_elem_value ev;
	unsigned int count, i, found = 0;

	ids = list_ids(&count);
	if (!ids)
		return 1;
	for (i = 0; i < count; i++) {
		if (strcmp((char *)ids[i].name, name))
			continue;
		found = 1;
		if (get_info(&ids[i], &ei) < 0)
			continue;
		memset(&ev, 0, sizeof(ev));
		ev.id = ids[i];
		for (unsigned int j = 0; j < ei.count; j++)
			ev.value.integer.value[j] = value;
		if (put_value(&ev) < 0)
			fprintf(stderr, "pbmix: set %s: %s\n", name,
				strerror(errno));
		else
			printf("set %s = %ld\n", name, value);
	}
	free(ids);
	return found ? 0 : 1;
}

static int set_enum(const char *name, const char *text)
{
	struct snd_ctl_elem_id *ids;
	struct snd_ctl_elem_info ei;
	struct snd_ctl_elem_value ev;
	unsigned int count, i, item;
	int found = 0;

	ids = list_ids(&count);
	if (!ids)
		return 1;
	for (i = 0; i < count; i++) {
		if (strcmp((char *)ids[i].name, name))
			continue;
		found = 1;
		if (get_info(&ids[i], &ei) < 0)
			continue;
		if (ei.type != SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
			fprintf(stderr, "pbmix: %s is not an enum\n", name);
			continue;
		}
		for (item = 0; item < ei.value.enumerated.items; item++) {
			char nm[64];

			if (get_item_name(&ids[i], item, nm, sizeof(nm)) < 0)
				continue;
			if (strcasecmp(nm, text))
				continue;
			memset(&ev, 0, sizeof(ev));
			ev.id = ids[i];
			for (unsigned int j = 0; j < ei.count; j++)
				ev.value.enumerated.item[j] = item;
			if (put_value(&ev) < 0)
				fprintf(stderr, "pbmix: enum %s: %s\n", name,
					strerror(errno));
			else
				printf("enum %s = %s\n", name, text);
			break;
		}
	}
	free(ids);
	return found ? 0 : 1;
}

static void match_set(const char *substr, long value)
{
	struct snd_ctl_elem_id *ids;
	unsigned int count, i;

	ids = list_ids(&count);
	if (!ids)
		return;
	for (i = 0; i < count; i++) {
		struct snd_ctl_elem_info ei;

		if (!strstr((char *)ids[i].name, substr))
			continue;
		if (get_info(&ids[i], &ei) < 0)
			continue;
		if (ei.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN)
			continue;
		if (set_control((char *)ids[i].name, value) != 0)
			perror("set");
	}
	free(ids);
}

static void list_match(const char *substr)
{
	struct snd_ctl_elem_id *ids;
	unsigned int count, i;

	ids = list_ids(&count);
	if (!ids)
		return;
	for (i = 0; i < count; i++) {
		if (substr && !strstr((char *)ids[i].name, substr))
			continue;
		print_one(&ids[i]);
	}
	free(ids);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"usage: pbmix list [substr] | on <substr> | off <substr> | "
			"set <name> <value> | enum <name> <text>\n");
		return 2;
	}

	fd = open(CTL_DEV, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "pbmix: open %s: %s\n", CTL_DEV,
			strerror(errno));
		return 1;
	}

	if (!strcmp(argv[1], "list"))
		list_match(argc > 2 ? argv[2] : NULL);
	else if (!strcmp(argv[1], "on") && argc > 2)
		match_set(argv[2], 1);
	else if (!strcmp(argv[1], "off") && argc > 2)
		match_set(argv[2], 0);
	else if (!strcmp(argv[1], "set") && argc > 3)
		return set_control(argv[2], strtol(argv[3], NULL, 0));
	else if (!strcmp(argv[1], "enum") && argc > 3)
		return set_enum(argv[2], argv[3]);
	else {
		fprintf(stderr, "pbmix: bad arguments\n");
		return 2;
	}

	close(fd);
	return 0;
}
