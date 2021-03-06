/**
 * Copyright 2012-2013(c) Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef IIO_THREADS
#include <glib/gthread.h>
#endif

#include "iio_utils.h"

#define MAX_STR_LEN		512

#ifndef IIO_THREADS
# define MAX_THREADS             1
#else
# define MAX_THREADS             10
static GThread *thread_ids[MAX_THREADS];
#endif

static char dev_dir_name[MAX_THREADS][MAX_STR_LEN];
static char buf_dir_name[MAX_THREADS][MAX_STR_LEN];
static char buffer_access[MAX_THREADS][MAX_STR_LEN];
static char last_device_name[MAX_THREADS][MAX_STR_LEN];
static char last_debug_name[MAX_THREADS][MAX_STR_LEN];
static char debug_dir_name[MAX_THREADS][MAX_STR_LEN];

#ifndef IIO_THREADS
static inline int thread_index()
{
	return 0;
}
#else
static int thread_index()
{
	GThread *t;
	size_t i;

	t = g_thread_self();

	for (i = 0; i < MAX_THREADS; i++) {
		if (thread_ids[i] == t)
			return i;
	}

	/* No existing threads, so let's see if an empty one exists */
	for (i = 0; i < MAX_THREADS; i++) {
		if (!thread_ids[i]) {
			thread_ids[i] = t;
			/* First time, so clear everything */
			dev_dir_name[i][0] = '\0';
			buffer_access[i][0] = '\0';
			last_device_name[i][0] = '\0';
			buf_dir_name[i][0] = '\0';
			last_debug_name[i][0] = '\0';
			debug_dir_name[i][0] = '\0';
			return i;
		}
	}

	printf("Too many threads - sorry\n");
	exit(0);
	return 0;
}

void iio_thread_clear(GThread *thread)
{
	size_t i;

	for (i = 0; i < MAX_THREADS; i++) {
		if (thread_ids[i] == thread) {
			thread_ids[i] = 0;
			return;
		}
	}
}
#endif

const char * dev_name_dir(void) {
	return dev_dir_name[thread_index()];
}

int set_dev_paths(const char *device_name)
{
	int dev_num, ret;
	struct stat s;
	size_t thr = thread_index();

	if (!device_name) {
		ret = -EFAULT;
		goto error_ret;
	}

	ret = stat(iio_dir, &s);
	if (ret) {
		ret = -EFAULT;
		goto error_ret;
	}

	if (strncmp(device_name, last_device_name[thr], MAX_STR_LEN) != 0) {
	/* Find the device requested */
		dev_num = find_type_by_name(device_name, "iio:device");
		if (dev_num >= 0) {
			ret = snprintf(buf_dir_name[thr], MAX_STR_LEN,"%siio:device%d/buffer",
					iio_dir, dev_num);
			if (ret >= MAX_STR_LEN) {
				syslog(LOG_ERR, "set_dev_paths failed (%d)\n", __LINE__);
				ret = -EFAULT;
				goto error_ret;
			}
			snprintf(dev_dir_name[thr], MAX_STR_LEN, "%siio:device%d",
					iio_dir, dev_num);
			snprintf(buffer_access[thr], MAX_STR_LEN, "/dev/iio:device%d",
					dev_num);
			strcpy(last_device_name[thr], device_name);
		} else {
			dev_num = find_type_by_name(device_name, "trigger");
			if (dev_num >= 0) {
				snprintf(dev_dir_name[thr], MAX_STR_LEN, "%strigger%d",
						iio_dir, dev_num);
				strcpy(last_device_name[thr], device_name);
			} else {
				syslog(LOG_ERR, "set_dev_paths failed to find the %s\n",
					device_name);
				ret = -ENODEV;
				goto error_ret;
			}
		}
	}

	return 0;

error_ret:
	dev_dir_name[thr][0] = '\0';
	buffer_access[thr][0] = '\0';
	last_device_name[thr][0] = '\0';
	return ret;
}

int set_debugfs_paths(const char *device_name)
{
	int dev_num, ret;
	FILE *debugfsfp;
	size_t thr;

	thr = thread_index();

	if (strncmp(device_name, last_debug_name[thr], MAX_STR_LEN) != 0) {
		/* Find the device requested */
		dev_num = find_type_by_name(device_name, "iio:device");
		if (dev_num < 0) {
			syslog(LOG_ERR, "%s failed to find the %s\n",
				__func__, device_name);
			ret = -ENODEV;
			goto error_ret;
		}
		ret = snprintf(debug_dir_name[thr], MAX_STR_LEN,"%siio:device%d/",
		iio_debug_dir, dev_num);
		if (ret >= MAX_STR_LEN) {
			syslog(LOG_ERR, "%s failed (%d)\n", __func__, __LINE__);
			ret = -EFAULT;
			goto error_ret;
		}
		debugfsfp = fopen(debug_dir_name[thr], "r");
		if (!debugfsfp) {
			syslog(LOG_ERR, "%s can't open %s\n", __func__, debug_dir_name[thr]);
			ret = -ENODEV;
			goto error_ret;
		}
	}
	return 0;

error_ret:
	debug_dir_name[thr][0] ='\0';
	return ret;
}

const char *debug_name_dir(void) {
	return debug_dir_name[thread_index()];
}

int read_reg(unsigned int address)
{
	size_t thr = thread_index();

	if (strlen(debug_dir_name[thr]) == 0)
		return 0;

	write_sysfs_int("direct_reg_access", debug_dir_name[thr], address);
	return read_sysfs_posint("direct_reg_access", debug_dir_name[thr]);
}

int write_reg(unsigned int address, unsigned int val)
{
	char temp[40];
	size_t thr = thread_index();

	if (strlen(debug_dir_name[thr]) == 0)
		return 0;

	sprintf(temp, "0x%x 0x%x\n", address, val);
	return write_sysfs_string("direct_reg_access", debug_dir_name[thr], temp);
}

/* returns true if needle is inside haystack */
static inline bool element_substr(const char *haystack, const char * end, const char *needle)
{
	size_t i;
	char ssub[256], esub[256], need[256];

	if ((strlen(needle) > sizeof (need)) ||
	    (end && (strlen(needle) + strlen(end) > sizeof(need)))) {
		printf("error in %s\n", __func__);
		exit(-1);
	}
	strcpy(need, needle);
	if (end)
		strcat(need, end);

	if (!strcmp(haystack, need))
		return true;

	/* split the string, and look for it */
	for (i = 0; i < strlen(need); i++) {
		sprintf(ssub, "%.*s", i, need);
		sprintf(esub, "%.*s", (int)(strlen(need) - i), need + i);
		if ((strstr(haystack, ssub) == haystack) &&
		    ((strstr(haystack, esub) + strlen(esub)) == (haystack + strlen(haystack))))
			return true;
	}
	return false;
}

#if 0
static void dump_str(char *str, size_t size)
{
	size_t i, j=0;

	printf("'");
	for (i = 0; i <= size; i++)
		if (str[i])
			printf("%c", str[i]);
		else {
			printf("'(%d|%d) '", j, i);
			j++;
		}

	printf("' : %d (%d)\n", size, j);
}
#endif

/*
* make sure the "_available" is right after the control
* IIO core doesn't make this happen in a normal sort
* since we can have indexes sometimes missing:
* out_altvoltage_1B_scale_available links to
* out_altvoltage1_1B_scale  and
* one _available, linking to multiple elements:
* in_voltage_test_mode_available links to both:
* in_voltage0_test_mode and in_voltage1_test_mode
*/
void scan_elements_insert(char **elements, char *token, char *end)
{
	char key[256], entire_key[256], *loop, *added = NULL;
	char *start, *next;
	int j, k, num2;
	size_t i, len, num = 0;

	if (!*elements)
		return;

	start = *elements;
	len = strlen(start);

	/* strip everything apart, to make it easier to work on */
	next = strtok(start, " ");
	while (next && (start + len) > next) {
		num++;
		next = strtok(NULL, " ");
	}

	/* now walk through things, looking for the token */
	for (i = 0; i < num; i++) {
		next = strstr(start, token);
		if (next) {
			if(added) {
				/* did we all ready process this one? */
				if (strstr(added, start)) {
					start += strlen(start) + 1;
					continue;
				}
				added = realloc(added, strlen(added) + strlen (start) + 1);
				strcat(added, start);
			} else
				added = strdup(start);

			if (strlen(start) >= sizeof(entire_key) - 1) {
				printf("key '%s' too large (%d) , can't fit in %d buffer in %s\n",
						start, strlen(start), sizeof(entire_key), __func__);
				exit(-1);
			}
			/* example : entire_key: 'in_temp0_scale'
			 *                  key: 'in_temp0'
			 */
			strcpy(entire_key, start);
			sprintf(key, "%.*s", (int)(next - start), start);

			/* Now we know where things are, we remove the entire_key from the buffer,
			 * so we don't process the key by accident
			 */
			memmove(start, start + strlen(entire_key) + 1, len - (start - *elements) - strlen(entire_key));
			len -= (strlen(entire_key) + 1);
			num--;

			/*  so we need to:
			 *  - find out where it goes (can go multiple places)
			 *  - add it to all the places where it needs to go
			 *  - update the pointers, since we may have realloc'ed things
			 */
			next = *elements;
			loop = NULL;
			k = 0;
			/* scan through entire list */
			num2 = num;
			for (j = 0; j < num2; j++) {
				if (element_substr(next, end, key)) {
					/* Now we need to make things bigger, and since realloc, can move
					 * things, we need to update all the pointers afterwards
					 */
					k = next - *elements;
					/* make the buffer bigger, by the length of the key to insert
					 * make sure to count the terminating null char
					 */
					*elements = realloc(*elements, (len + 1) + (strlen(entire_key) + 1));
					/* reset the pointers */
					next = *elements + k;
					/* move the buffer into the extended position */
					loop = next + strlen(next) + 1;
					memmove(loop + strlen(entire_key) + 1, loop, (len + 1) - (loop - *elements));
					/* Move the key into the opening */
					strcpy(loop, entire_key);

					/* increment things */
					len += strlen(entire_key) + 1;
					num++; num2++; j++;
					/* Get read for the next one */
					start = loop;
					next += strlen(next) + 1;
				}
				/* process next one */
				next += strlen(next) + 1;
			}
			start = *elements;
			i = 0;
		}
		start += strlen(start) + 1;
	}

	start = *elements;

	/* put everything back together */
	for (i = 0; i < len; i++) {
		if (start[i] == 0)
			start[i] = ' ';
	}

	start[len] = 0;

	if (len != strlen(start))
		fprintf(stderr, "error in %s(%s)\n", __FILE__, __func__);

	if (added)
		free(added);
}

void scan_elements_sort(char **elements)
{
	int swap;
	size_t i, j, k, len, num = 0;
	char *start, *next, *loop, temp[256];

	if (!*elements)
		return;

	next = start = *elements;

	len = strlen(start);

	/* strip everything apart, to make it easier to work on */
	next = strtok(start, " ");
	while (next) {
		num++;
		next = strtok(NULL, " ");
	}

	/*
	 * sort things using bubble sort
	 * there are plenty ways more efficent to do this - knock yourself out
	 */
	for (j = 0; j < num - 1; j++) {
		start = *elements;
		/* make sure dev, name, uevent are first (if they exist) */
		while (!strcmp(start, "name") || !strcmp(start, "dev") || !strcmp(start, "uevent")) {
			start += strlen(start) + 1;
		}

		loop = start;
		next = start + strlen(start) + 1;
		for (i = j; (i < num - 1) && (strlen(start)) && (strlen(next)); i++) {
			if (!strcmp(next, "name") || !strcmp(next, "dev") || !strcmp(next, "uevent")) {
				strcpy(temp, next);
				memmove(loop + strlen(temp) + 1, loop, next - loop - 1);
				strcpy(loop, temp);
				loop += strlen(temp) + 1;
			} else {
				swap = 0;
				/* Can't use strcmp, since it doesn't sort numerically */
				for (k = 0; k < strlen(start) && k < strlen(next); k++) {
					if (start[k] == next[k])
						continue;

					/* sort LABEL0_ LABEL10_ as zero and ten */
					if ((isdigit(start[k]) && isdigit(next[k])) &&
					    (isdigit(start[k+1]) || isdigit(next[k+1]))){
						if (atoi(&start[k]) >= atoi(&next[k])) {
							swap = 1;
						}
					} else if (start[k] >= next[k]) {
						swap = 1;
					}

					break;
				}
				if (k == strlen(next))
					swap = 1;

				if (swap) {
					strcpy(temp, start);
					memmove(start, next, strlen(next) +1);
					next = start + strlen(start) + 1;
					strcpy(next, temp);
				}
			}
			start += strlen(start) + 1;
			next = start + strlen(start) + 1;
		}
	}

	start = *elements;

	/* put everything back together */
	for (i = 0; i < len; i++) {
		if (start[i] == 0)
			start[i] = ' ';
	}
	start[len] = 0;

	if (len != strlen(start))
		fprintf(stderr, "error in %s(%s)\n", __FILE__, __func__);

}

int find_scan_elements(char *dev, char **relement, unsigned access)
{
	FILE *fp;
	char elements[128], buf[128];
	char *elem = NULL;
	int num = 0;

	/* flushes all open output streams */
	 fflush(NULL);

	sprintf(buf, "echo \"%s %s . \" | iio_cmdsrv",
		access ? "dbfsshow" : "show", dev);
	fp = popen(buf, "r");
	if(fp == NULL) {
		fprintf(stderr, "Can't execute iio_cmdsrv\n");
		return -ENODEV;
	}


	elem = malloc(128);
	memset (elem, 0, 128);

	while(fgets(elements, sizeof(elements), fp) != NULL){
		/* strip trailing new lines */
		if (elements[strlen(elements) - 1] == '\n')
			elements[strlen(elements) - 1] = '\0';

		/* first thing returned is the return code */
		if (num == 0) {
			num ++;
			continue;
		}

		elem = realloc(elem, strlen(elements) + strlen(elem) + 1);
		strncat(elem, elements, 128);
	}

	if (relement)
		*relement = elem;

	return 1;
}

int read_sysfs_string(const char *filename, const char *basedir, char **str)
{
	int ret = 0, len;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);

	if (temp == NULL) {
		syslog(LOG_ERR, "Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", basedir, filename);

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		syslog(LOG_ERR, "could not open file to verify\n");
		ret = -errno;
		goto error_free;
	}
	*str = calloc(1024, 1);
	ret = fread(*str, 1024, 1, sysfsfp);

	if (ret < 0) {
		if (NULL != *str)
			free(*str);

	}

	/* if the last char is a newline, eat it */
	len = strlen(*str);
	if ((*str)[len - 1] == '\n') {
		(*str)[len - 1] = '\0';
		len = strlen(*str);
	}

	fclose(sysfsfp);

	/* zero elements, which are zero length is an error */
	if (ret == 0 && len == 0)
		ret = -EINVAL;
	else
		ret = len;

error_free:
	free(temp);
	return ret;
}

int write_devattr(const char *attr, const char *str)
{
	int ret;
	size_t thr = thread_index();

	if (strlen(dev_dir_name[thr]) == 0)
		return -ENODEV;

	ret = write_sysfs_string(attr, dev_dir_name[thr], str);

	if (ret < 0) {
		syslog(LOG_ERR, "write_devattr failed (%d)\n", __LINE__);
	}

	return ret;
}

int read_devattr(const char *attr, char **str)
{
	int ret;
	size_t thr = thread_index();

	if (strlen(dev_dir_name[thr]) == 0)
		return -ENODEV;

	ret = read_sysfs_string(attr, dev_dir_name[thr], str);
	if (ret < 0) {
		syslog(LOG_ERR, "read_devattr failed (%d)\n", __LINE__);
	}

	return ret;
}

int read_devattr_bool(const char *attr, bool *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);
	if (ret < 0)
		return ret;

	if (buf[0] == '1' && buf[1] == '\0')
		*value = true;
	else
		*value = false;
	free(buf);

	return 0;
}

int read_devattr_double(const char *attr, double *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);

	if (ret < 0)
		return ret;

	sscanf(buf, "%lf", value);
	free(buf);

	return 0;
}

int write_devattr_double(const char *attr, double value)
{
	char buf[100];

	snprintf(buf, 100, "%f", value);
	return write_devattr(attr, buf);
}

int read_devattr_slonglong(const char *attr, long long *value)
{
	char *buf;
	int ret;

	ret = read_devattr(attr, &buf);
	if (ret < 0)
		return ret;

	sscanf(buf, "%lli", value);
	free(buf);

	return 0;
}

int write_devattr_slonglong(const char *attr, long long value)
{
	char buf[100];

	snprintf(buf, 100, "%lld", value);
	return write_devattr(attr, buf);
}

int write_devattr_int(const char *attr, unsigned long long value)
{
	char buf[100];

	snprintf(buf, 100, "%llu", value);
	return write_devattr(attr, buf);
}

int read_devattr_int(char *attr, int *val)
{
	int ret;
	size_t thr = thread_index();

	if (strlen(dev_dir_name[thr]) == 0)
		return -ENODEV;

	ret = read_sysfs_posint(attr, dev_dir_name[thr]);
	if (ret < 0) {
		syslog(LOG_ERR, "read_devattr failed (%d)\n", __LINE__);
	}

	*val = ret;

	return ret;
}

bool iio_devattr_exists(const char *device, const char *attr)
{
	char *temp;
	struct stat s;
	size_t thr = thread_index();
	int ret;

	set_dev_paths(device);

	if (strlen(dev_dir_name[thr]) == 0)
		return -ENODEV;

	temp = malloc(strlen(dev_dir_name[thr]) + strlen(attr) + 2);
	if (temp == NULL) {
		fprintf(stderr, "Memory allocation failed\n");
		return -ENOMEM;
	}
	sprintf(temp, "%s/%s", dev_dir_name[thr], attr);

	ret = stat(temp, &s);

	free(temp);

	if (ret != 0)
		return false;

	return S_ISREG(s.st_mode);
}

int iio_buffer_open(bool read, int flags)
{
	if (read)
		flags |= O_RDONLY;
	else
		flags |= O_WRONLY;

	return open(buffer_access[thread_index()], flags);
}
