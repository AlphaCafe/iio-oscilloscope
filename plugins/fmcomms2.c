/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 **/
#include <stdio.h>

#include <gtk/gtk.h>
#include <gtkdatabox.h>
#include <glib/gthread.h>
#include <gtkdatabox_grid.h>
#include <gtkdatabox_points.h>
#include <gtkdatabox_lines.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <malloc.h>
#include <values.h>
#include <sys/stat.h>

#include "../datatypes.h"
#include "../osc.h"
#include "../iio_widget.h"
#include "../iio_utils.h"
#include "../osc_plugin.h"
#include "../config.h"
#include "../eeprom.h"
#include "./block_diagram.h"

#define HANNING_ENBW 1.50

extern gfloat plugin_fft_corr;

static bool is_2rx_2tx;

static const gdouble mhz_scale = 1000000.0;
static const gdouble khz_scale = 1000.0;
static const gdouble inv_scale = -1.0;
static char *dac_buf_filename = NULL;

static bool dac_data_loaded = false;

static struct iio_widget glb_widgets[50];
static struct iio_widget tx_widgets[50];
static struct iio_widget rx_widgets[50];
static unsigned int rx1_gain, rx2_gain;
static unsigned int num_glb, num_tx, num_rx;
static unsigned int rx_lo, tx_lo;

/* Widgets for Global Settings */
static GtkWidget *ensm_mode;
static GtkWidget *ensm_mode_available;
static GtkWidget *calib_mode;
static GtkWidget *calib_mode_available;
static GtkWidget *trx_rate_governor;
static GtkWidget *trx_rate_governor_available;
static GtkWidget *filter_fir_config;
static GtkWidget *dac_buffer;

/* Widgets for Receive Settings */
static GtkWidget *rx_gain_control_rx1;
static GtkWidget *rx_gain_control_modes_rx1;
static GtkWidget *rf_port_select_rx;
static GtkWidget *rx_gain_control_rx2;
static GtkWidget *rx_gain_control_modes_rx2;
static GtkWidget *rx1_rssi;
static GtkWidget *rx2_rssi;
static GtkWidget *rx_path_rates;
static GtkWidget *tx_path_rates;
static GtkWidget *fir_filter_en_tx;
static GtkWidget *enable_fir_filter_rx;
static GtkWidget *rf_port_select_tx;
static GtkWidget *rx_fastlock_profile;
static GtkWidget *tx_fastlock_profile;

/* Widgets for Receive Settings */
static GtkWidget *dds_mode;
static GtkWidget *dds1_freq, *dds2_freq, *dds3_freq, *dds4_freq,
                 *dds5_freq, *dds6_freq, *dds7_freq, *dds8_freq;
static GtkWidget *dds1_scale, *dds2_scale, *dds3_scale, *dds4_scale,
                 *dds5_scale, *dds6_scale, *dds7_scale, *dds8_scale;
static GtkWidget *dds1_phase, *dds2_phase, *dds3_phase, *dds4_phase,
                 *dds5_phase, *dds6_phase, *dds7_phase, *dds8_phase;
static GtkWidget *dds1_freq, *dds2_freq, *dds3_freq, *dds4_freq,
                 *dds5_freq, *dds6_freq, *dds7_freq, *dds8_freq;
static GtkAdjustment *adj1_freq, *adj2_freq, *adj3_freq, *adj4_freq,
                     *adj5_freq, *adj6_freq, *adj7_freq, *adj8_freq;

static GtkWidget *dds1_freq_l, *dds2_freq_l, *dds3_freq_l, *dds4_freq_l,
                 *dds5_freq_l, *dds6_freq_l, *dds7_freq_l, *dds8_freq_l;
static GtkWidget *dds1_scale_l, *dds2_scale_l, *dds3_scale_l, *dds4_scale_l,
                 *dds5_scale_l, *dds6_scale_l, *dds7_scale_l, *dds8_scale_l;
static GtkWidget *dds1_phase_l, *dds2_phase_l, *dds3_phase_l, *dds4_phase_l,
                 *dds5_phase_l, *dds6_phase_l, *dds7_phase_l, *dds8_phase_l;
static GtkWidget *dds_I_TX1_l, *dds_I1_TX1_l, *dds_I2_TX1_l,
                 *dds_I_TX2_l, *dds_I1_TX2_l, *dds_I2_TX2_l;
static GtkWidget *dds_Q_TX1_l, *dds_Q1_TX1_l, *dds_Q2_TX1_l,
                 *dds_Q_TX2_l, *dds_Q1_TX2_l, *dds_Q2_TX2_l;

static gulong dds1_freq_hid = 0, dds2_freq_hid = 0, dds5_freq_hid = 0, dds6_freq_hid = 0;
static gulong dds1_scale_hid = 0, dds2_scale_hid = 0, dds5_scale_hid = 0, dds6_scale_hid = 0;
static gulong dds1_phase_hid = 0, dds2_phase_hid = 0, dds5_phase_hid = 0, dds6_phase_hid = 0;

static gint this_page;
static GtkNotebook *nbook;
static gboolean plugin_detached;

static char last_fir_filter[PATH_MAX];

static void tx_update_values(void)
{
	iio_update_widgets(tx_widgets, num_tx);
}

static void rx_update_values(void)
{
	iio_update_widgets(rx_widgets, num_rx);
	rx_update_labels();
}

static void glb_settings_update_labels(void)
{
	char *buf = NULL;
	float rates[6];
	char tmp[160];
	int ret;

	set_dev_paths("ad9361-phy");
	ret = read_devattr("ensm_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(ensm_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(ensm_mode), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("calib_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(calib_mode), buf);
	else
		gtk_label_set_text(GTK_LABEL(calib_mode), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("trx_rate_governor", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), buf);
	else
		gtk_label_set_text(GTK_LABEL(trx_rate_governor), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	ret = read_devattr("in_voltage0_gain_control_mode", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx_gain_control_rx1), "<error>");
	if (buf)
		free(buf);

	if (is_2rx_2tx) {
		ret = read_devattr("in_voltage1_gain_control_mode", &buf);
		if (ret >= 0)
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), buf);
		else
			gtk_label_set_text(GTK_LABEL(rx_gain_control_rx2), "<error>");
		if (buf)
			free(buf);
	}

	ret = read_devattr("rx_path_rates", &buf);
	if (ret >= 0) {
		sscanf(buf, "BBPLL:%f ADC:%f R2:%f R1:%f RF:%f RXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   ADC: %4.3f   R2: %4.3f   R1: %4.3f   RF: %4.3f   RXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(rx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(rx_path_rates), "<error>");
	}
	if (buf)
		free(buf);

	ret = read_devattr("tx_path_rates", &buf);
	if (ret >= 0) {
		sscanf(buf, "BBPLL:%f DAC:%f T2:%f T1:%f TF:%f TXSAMP:%f",
		        &rates[0], &rates[1], &rates[2], &rates[3], &rates[4],
			&rates[5]);
		sprintf(tmp, "BBPLL: %4.3f   DAC: %4.3f   T2: %4.3f   T1: %4.3f   TF: %4.3f   TXSAMP: %4.3f",
		        rates[0] / 1e6, rates[1] / 1e6, rates[2] / 1e6,
			rates[3] / 1e6, rates[4] / 1e6, rates[5] / 1e6);

		gtk_label_set_text(GTK_LABEL(tx_path_rates), tmp);
	} else {
		gtk_label_set_text(GTK_LABEL(tx_path_rates), "<error>");
	}
	if (buf)
		free(buf);

}

static void rssi_update_labels(void)
{
	char *buf = NULL;
	int ret;

	set_dev_paths("ad9361-phy");
	ret = read_devattr("in_voltage0_rssi", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx1_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx1_rssi), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

	if (!is_2rx_2tx)
		return;

	ret = read_devattr("in_voltage1_rssi", &buf);
	if (ret >= 0)
		gtk_label_set_text(GTK_LABEL(rx2_rssi), buf);
	else
		gtk_label_set_text(GTK_LABEL(rx2_rssi), "<error>");
	if (buf) {
		free(buf);
		buf = NULL;
	}

}

static void update_display (void *ptr)
{
	const char *gain_mode;

	/* This thread never exists, and just updates the control frame */
	while (1) {
		if (this_page == gtk_notebook_get_current_page(nbook) || plugin_detached) {
			gdk_threads_enter();
			rssi_update_labels();
			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx1));
			if (gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx1_gain]);

			gain_mode = gtk_combo_box_get_active_text(GTK_COMBO_BOX(rx_gain_control_modes_rx2));
			if (is_2rx_2tx && gain_mode && strcmp(gain_mode, "manual"))
				iio_widget_update(&rx_widgets[rx2_gain]);
			gdk_threads_leave();
		}
		sleep(1);
	}
}

void filter_fir_update(void)
{
	bool rx, tx;

	set_dev_paths("ad9361-phy");
	read_devattr_bool("in_voltage_filter_fir_en", &rx);
	read_devattr_bool("out_voltage_filter_fir_en", &tx);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx), rx);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx), tx);
}

void filter_fir_enable(void)
{
	bool rx, tx;

	rx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (enable_fir_filter_rx));
	tx = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON (fir_filter_en_tx));

	set_dev_paths("ad9361-phy");

	if (rx == tx) {
		write_devattr("in_out_voltage_filter_fir_en", rx ? "1" : "0");
	} else {
		write_devattr("out_voltage_filter_fir_en", tx ? "1" : "0");
		write_devattr("in_voltage_filter_fir_en", rx ? "1" : "0");

	}

	filter_fir_update();
}

static void save_button_clicked(GtkButton *btn, gpointer data)
{
	iio_save_widgets(glb_widgets, num_glb);
	filter_fir_enable();
	iio_save_widgets(tx_widgets, num_tx);
	iio_save_widgets(rx_widgets, num_rx);
	rx_update_labels();
	glb_settings_update_labels();
	rssi_update_labels();
}

static void reload_button_clicked(GtkButton *btn, gpointer data)
{
	iio_update_widgets(glb_widgets, num_glb);
	iio_update_widgets(tx_widgets, num_tx);
	iio_update_widgets(rx_widgets, num_rx);
	filter_fir_update();
	rx_update_labels();
	glb_settings_update_labels();
	rssi_update_labels();
}

static void hide_section_cb(GtkToggleToolButton *btn, GtkWidget *section)
{
	GtkWidget *toplevel;

	if (gtk_toggle_tool_button_get_active(btn)) {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-down", NULL);
		gtk_widget_show(section);
	} else {
		gtk_object_set(GTK_OBJECT(btn), "stock-id", "gtk-go-up", NULL);
		gtk_widget_hide(section);
		toplevel = gtk_widget_get_toplevel(GTK_WIDGET(btn));
		if (GTK_WIDGET_TOPLEVEL(toplevel))
			gtk_window_resize (GTK_WINDOW(toplevel), 1, 1);
	}
}

static void fastlock_clicked(GtkButton *btn, gpointer data)
{
	int profile;

	switch ((int)data) {
		case 1: /* RX Store */
			iio_widget_save(&rx_widgets[rx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage0_RX_LO_fastlock_store", profile);
			break;
		case 2: /* TX Store */
			iio_widget_save(&tx_widgets[tx_lo]);
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage1_TX_LO_fastlock_store", profile);
			break;
		case 3: /* RX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(rx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage0_RX_LO_fastlock_recall", profile);
			iio_widget_update(&rx_widgets[rx_lo]);
			break;
		case 4: /* TX Recall */
			profile = gtk_combo_box_get_active(GTK_COMBO_BOX(tx_fastlock_profile));
			set_dev_paths("ad9361-phy");
			write_devattr_int("out_altvoltage1_TX_LO_fastlock_recall", profile);
			iio_widget_update(&tx_widgets[tx_lo]);
			break;
	}
}

static void load_fir_filter(const char *file_name)
{
	char str[4096];
	int ret;

	set_dev_paths("ad9361-phy");
	sprintf(str, "cat %s > %s/filter_fir_config ", file_name, dev_name_dir());
	ret = system(str);
	if (ret < 0)
		fprintf(stderr, "FIR filter config failed\n");
	else
		strcpy(last_fir_filter, file_name);
}

void filter_fir_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);

	load_fir_filter(file_name);
}

short convert(double scale, float val)
{
	return (short) (val * scale);
}

int analyse_wavefile(const char *file_name, char **buf, int *count)
{
	int ret, j, i = 0, size, rep, tx = is_2rx_2tx ? 2 : 1;
	double max = 0.0, val[4], scale = 0.0;
	double i1, q1, i2, q2;
	char line[80];

	FILE *infile = fopen(file_name, "r");
	if (infile == NULL)
		return -3;

	if (fgets(line, 80, infile) != NULL) {
	if (strncmp(line, "TEXT", 4) == 0) {
		/* Unscaled samples need to be in the range +- 2047 */
		if (strncmp(line, "TEXTU", 5) == 0)
			scale = 16.0; /* scale up to 16-bit */
		ret = sscanf(line, "TEXT%*c REPEAT %d", &rep);
		if (ret != 1) {
			rep = 1;
		}
		size = 0;
		while (fgets(line, 80, infile)) {
			ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
				     &val[0], &val[1], &val[2], &val[3]);

			if (!(ret == 4 || ret == 2)) {
				fclose(infile);
				return -2;
			}

			for (i = 0; i < ret; i++)
				if (fabs(val[i]) > max)
					max = fabs(val[i]);

			size += ((tx == 2) ? 8 : 4);


		}

	size *= rep;
	if (scale == 0.0)
		scale = 32767.0 / max;

	if (max > 32767.0)
		fprintf(stderr, "ERROR: DAC Waveform Samples > +/- 2047.0\n");

	*buf = malloc(size);
	if (*buf == NULL)
		return 0;

	unsigned long long *sample = *((unsigned long long **)buf);
	unsigned int *sample_32 = *((unsigned int **)buf);

	rewind(infile);

	if (fgets(line, 80, infile) != NULL) {
		if (strncmp(line, "TEXT", 4) == 0) {
			size = 0;
			i = 0;
			while (fgets(line, 80, infile)) {

				ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
					     &i1, &q1, &i2, &q2);
				for (j = 0; j < rep; j++) {
					if (ret == 4 && tx == 2) {
						sample[i++] = ((unsigned long long)convert(scale, q2) << 48) +
							((unsigned long long)convert(scale, i2) << 32) +
							(convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 8;
					}
					if (ret == 2 && tx == 2) {
						sample[i++] = ((unsigned long long)convert(scale, q1) << 48) +
							((unsigned long long)convert(scale, i1) << 32) +
							(convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 8;
					}
					if (tx == 1) {
						sample_32[i++] = (convert(scale, q1) << 16) +
							(convert(scale, i1) << 0);

						size += 4;
					}
				}
			}
		}
	}

	fclose(infile);
	*count = size;

	}} else {
		fclose(infile);
		*buf = NULL;
		return -1;
	}

	return 0;
}

static void process_dac_buffer_file (const char *file_name)
{
	int ret, fd, size;
	struct stat st;
	char *buf;
	FILE *infile;

	ret = analyse_wavefile(file_name, &buf, &size);
	if (ret == -3)
		return;

	if (ret == -1 || buf == NULL) {
		stat(file_name, &st);
		buf = malloc(st.st_size);
		if (buf == NULL)
			return;
		infile = fopen(file_name, "r");
		size = fread(buf, 1, st.st_size, infile);
		fclose(infile);
	}

	set_dev_paths("cf-ad9361-dds-core-lpc");
	write_devattr_int("buffer/enable", 0);

	fd = iio_buffer_open(false, 0);
	if (fd < 0) {
		free(buf);
		return;
	}

	ret = write(fd, buf, size);
	if (ret != size) {
		fprintf(stderr, "Loading waveform failed %d\n", ret);
	}

	close(fd);
	free(buf);

	ret = write_devattr_int("buffer/enable", 1);
	if (ret < 0) {
		fprintf(stderr, "Failed to enable buffer: %d\n", ret);
	}

	dac_data_loaded = true;

	if (dac_buf_filename)
		free(dac_buf_filename);
	dac_buf_filename = malloc(strlen(file_name) + 1);
	strcpy(dac_buf_filename, file_name);

}

static void dac_buffer_config_file_set_cb (GtkFileChooser *chooser, gpointer data)
{
	char *file_name = gtk_file_chooser_get_filename(chooser);
	if (file_name)
		process_dac_buffer_file((const char *)file_name);
}

static int compare_gain(const char *a, const char *b)
{
	double val_a, val_b;
	sscanf(a, "%lf", &val_a);
	sscanf(b, "%lf", &val_b);

	if (val_a < val_b)
		return -1;
	else if(val_a > val_b)
		return 1;
	else
		return 0;
}

static void dds_locked_freq_cb(GtkToggleButton *btn, gpointer data)
{
	gdouble freq1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_freq));
	gdouble freq2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_freq));
	gdouble freq5 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds5_freq));
	gdouble freq6 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds6_freq));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds6_freq), freq5);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds7_freq), freq5);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds8_freq), freq5);
			break;
		case 2:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_freq), freq1);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_freq), freq2);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds7_freq), freq5);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds8_freq), freq6);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}

static void dds_locked_phase_cb(GtkToggleButton *btn, gpointer data)
{

	gdouble phase1 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds1_phase));
	gdouble phase2 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds2_phase));
	gdouble phase5 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds5_phase));
	gdouble phase6 = gtk_spin_button_get_value(GTK_SPIN_BUTTON(dds6_phase));

	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds2_phase), phase1);
			if ((phase1 - 90) < 0)
				phase1 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 - 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase1 - 90.0);

			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds6_phase), phase5);
			if ((phase5 - 90) < 0)
				phase5 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds7_phase), phase5 - 90.0);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds8_phase), phase5 - 90.0);
			break;
		case 2:
			if ((phase1 - 90) < 0)
				phase1 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds3_phase), phase1 - 90.0);
			if ((phase2 - 90) < 0)
				phase2 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds4_phase), phase2 - 90.0);
			if ((phase5 - 90) < 0)
				phase5 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds7_phase), phase5 - 90.0);
			if ((phase6 - 90) < 0)
				phase6 += 360;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(dds8_phase), phase6 - 90.0);
			break;
		default:
			printf("%s: error\n", __func__);
			break;
	}
}

static void dds_locked_scale_cb(GtkComboBoxText *box, gpointer data)
{
	gint scale1 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds1_scale));
	gint scale2 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds2_scale));
	gint scale5 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds5_scale));
	gint scale6 = gtk_combo_box_get_active(GTK_COMBO_BOX(dds6_scale));


	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode))) {
		case 1:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds2_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds6_scale), scale5);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds7_scale), scale5);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds8_scale), scale5);
			break;
		case 2:
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds3_scale), scale1);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds4_scale), scale2);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds7_scale), scale5);
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds8_scale), scale6);
			break;
		default:
			break;
	}
}

static void hide_all_I_and_Q(void)
{
	gtk_widget_hide(dds1_freq);
	gtk_widget_hide(dds2_freq);
	gtk_widget_hide(dds3_freq);
	gtk_widget_hide(dds4_freq);
	gtk_widget_hide(dds5_freq);
	gtk_widget_hide(dds6_freq);
	gtk_widget_hide(dds7_freq);
	gtk_widget_hide(dds8_freq);
	gtk_widget_hide(dds1_scale);
	gtk_widget_hide(dds2_scale);
	gtk_widget_hide(dds3_scale);
	gtk_widget_hide(dds4_scale);
	gtk_widget_hide(dds5_scale);
	gtk_widget_hide(dds6_scale);
	gtk_widget_hide(dds7_scale);
	gtk_widget_hide(dds8_scale);
	gtk_widget_hide(dds1_phase);
	gtk_widget_hide(dds2_phase);
	gtk_widget_hide(dds3_phase);
	gtk_widget_hide(dds4_phase);
	gtk_widget_hide(dds5_phase);
	gtk_widget_hide(dds6_phase);
	gtk_widget_hide(dds7_phase);
	gtk_widget_hide(dds8_phase);
	gtk_widget_hide(dds1_freq_l);
	gtk_widget_hide(dds2_freq_l);
	gtk_widget_hide(dds3_freq_l);
	gtk_widget_hide(dds4_freq_l);
	gtk_widget_hide(dds5_freq_l);
	gtk_widget_hide(dds6_freq_l);
	gtk_widget_hide(dds7_freq_l);
	gtk_widget_hide(dds8_freq_l);
	gtk_widget_hide(dds1_scale_l);
	gtk_widget_hide(dds2_scale_l);
	gtk_widget_hide(dds3_scale_l);
	gtk_widget_hide(dds4_scale_l);
	gtk_widget_hide(dds5_scale_l);
	gtk_widget_hide(dds6_scale_l);
	gtk_widget_hide(dds7_scale_l);
	gtk_widget_hide(dds8_scale_l);
	gtk_widget_hide(dds1_phase_l);
	gtk_widget_hide(dds2_phase_l);
	gtk_widget_hide(dds3_phase_l);
	gtk_widget_hide(dds4_phase_l);
	gtk_widget_hide(dds5_phase_l);
	gtk_widget_hide(dds6_phase_l);
	gtk_widget_hide(dds7_phase_l);
	gtk_widget_hide(dds8_phase_l);
	gtk_widget_hide(dds_I_TX1_l);
	gtk_widget_hide(dds_I1_TX1_l);
	gtk_widget_hide(dds_I2_TX1_l);
	gtk_widget_hide(dds_I_TX2_l);
	gtk_widget_hide(dds_I1_TX2_l);
	gtk_widget_hide(dds_I2_TX2_l);
	gtk_widget_hide(dds_Q_TX1_l);
	gtk_widget_hide(dds_Q1_TX1_l);
	gtk_widget_hide(dds_Q2_TX1_l);
	gtk_widget_hide(dds_Q_TX2_l);
	gtk_widget_hide(dds_Q1_TX2_l);
	gtk_widget_hide(dds_Q2_TX2_l);
}

static void show_all_I_and_Q(void)
{
	gtk_widget_show(dds1_freq);
	gtk_widget_show(dds2_freq);
	gtk_widget_show(dds3_freq);
	gtk_widget_show(dds4_freq);
	gtk_widget_show(dds5_freq);
	gtk_widget_show(dds6_freq);
	gtk_widget_show(dds7_freq);
	gtk_widget_show(dds8_freq);
	gtk_widget_show(dds1_scale);
	gtk_widget_show(dds2_scale);
	gtk_widget_show(dds3_scale);
	gtk_widget_show(dds4_scale);
	gtk_widget_show(dds5_scale);
	gtk_widget_show(dds6_scale);
	gtk_widget_show(dds7_scale);
	gtk_widget_show(dds8_scale);
	gtk_widget_show(dds1_phase);
	gtk_widget_show(dds2_phase);
	gtk_widget_show(dds3_phase);
	gtk_widget_show(dds4_phase);
	gtk_widget_show(dds5_phase);
	gtk_widget_show(dds6_phase);
	gtk_widget_show(dds7_phase);
	gtk_widget_show(dds8_phase);
	gtk_widget_show(dds1_freq_l);
	gtk_widget_show(dds2_freq_l);
	gtk_widget_show(dds3_freq_l);
	gtk_widget_show(dds4_freq_l);
	gtk_widget_show(dds5_freq_l);
	gtk_widget_show(dds6_freq_l);
	gtk_widget_show(dds7_freq_l);
	gtk_widget_show(dds8_freq_l);
	gtk_widget_show(dds1_scale_l);
	gtk_widget_show(dds2_scale_l);
	gtk_widget_show(dds3_scale_l);
	gtk_widget_show(dds4_scale_l);
	gtk_widget_show(dds5_scale_l);
	gtk_widget_show(dds6_scale_l);
	gtk_widget_show(dds7_scale_l);
	gtk_widget_show(dds8_scale_l);
	gtk_widget_show(dds1_phase_l);
	gtk_widget_show(dds2_phase_l);
	gtk_widget_show(dds3_phase_l);
	gtk_widget_show(dds4_phase_l);
	gtk_widget_show(dds5_phase_l);
	gtk_widget_show(dds6_phase_l);
	gtk_widget_show(dds7_phase_l);
	gtk_widget_show(dds8_phase_l);
	gtk_widget_show(dds_I_TX1_l);
	gtk_widget_show(dds_I1_TX1_l);
	gtk_widget_show(dds_I2_TX1_l);
	gtk_widget_show(dds_I_TX2_l);
	gtk_widget_show(dds_I1_TX2_l);
	gtk_widget_show(dds_I2_TX2_l);
	gtk_widget_show(dds_Q_TX1_l);
	gtk_widget_show(dds_Q1_TX1_l);
	gtk_widget_show(dds_Q2_TX1_l);
	gtk_widget_show(dds_Q_TX2_l);
	gtk_widget_show(dds_Q1_TX2_l);
	gtk_widget_show(dds_Q2_TX2_l);
}

static void enable_dds(bool on_off)
{
	int ret;

	set_dev_paths("cf-ad9361-dds-core-lpc");
	write_devattr_int("out_altvoltage0_TX1_I_F1_raw", on_off ? 1 : 0);

	if (on_off || dac_data_loaded) {
		ret = write_devattr_int("buffer/enable", !on_off);
		if (ret < 0) {
			fprintf(stderr, "Failed to enable buffer: %d\n", ret);

		}
	}
}

static void manage_dds_mode()
{
	gint active;

	active = gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode));
	switch (active) {
	case 0:
		/* Disabled */
		enable_dds(false);
		hide_all_I_and_Q();
		gtk_widget_hide(dac_buffer);
		break;
	case 1:
		/* One tone */
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX1_l),"<b>Single Tone</b>");
		gtk_label_set_markup(GTK_LABEL(dds_I_TX2_l),"<b>Single Tone</b>");
		gtk_widget_show(dds1_freq);
		gtk_widget_hide(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_show(dds5_freq);
		gtk_widget_hide(dds6_freq);
		gtk_widget_hide(dds7_freq);
		gtk_widget_hide(dds8_freq);
		gtk_widget_show(dds1_scale);
		gtk_widget_hide(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_show(dds5_scale);
		gtk_widget_hide(dds6_scale);
		gtk_widget_hide(dds7_scale);
		gtk_widget_hide(dds8_scale);
		gtk_widget_hide(dds1_phase);
		gtk_widget_hide(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_hide(dds5_phase);
		gtk_widget_hide(dds6_phase);
		gtk_widget_hide(dds7_phase);
		gtk_widget_hide(dds8_phase);
		gtk_widget_show(dds1_freq_l);
		gtk_widget_hide(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_show(dds5_freq_l);
		gtk_widget_hide(dds6_freq_l);
		gtk_widget_hide(dds7_freq_l);
		gtk_widget_hide(dds8_freq_l);
		gtk_widget_show(dds1_scale_l);
		gtk_widget_hide(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_show(dds5_scale_l);
		gtk_widget_hide(dds6_scale_l);
		gtk_widget_hide(dds7_scale_l);
		gtk_widget_hide(dds8_scale_l);
		gtk_widget_hide(dds1_phase_l);
		gtk_widget_hide(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_hide(dds5_phase_l);
		gtk_widget_hide(dds6_phase_l);
		gtk_widget_hide(dds7_phase_l);
		gtk_widget_hide(dds8_phase_l);
		gtk_widget_show(dds_I_TX1_l);
		gtk_widget_show(dds_I1_TX1_l);
		gtk_widget_hide(dds_I2_TX1_l);
		gtk_widget_show(dds_I_TX2_l);
		gtk_widget_show(dds_I1_TX2_l);
		gtk_widget_hide(dds_I2_TX2_l);
		gtk_widget_hide(dds_Q_TX1_l);
		gtk_widget_hide(dds_Q1_TX1_l);
		gtk_widget_hide(dds_Q2_TX1_l);
		gtk_widget_hide(dds_Q_TX2_l);
		gtk_widget_hide(dds_Q1_TX2_l);
		gtk_widget_hide(dds_Q2_TX2_l);

		/* Connect the widgets that are showing (1 & 5) */
#define IIO_SPIN_SIGNAL "value-changed"
#define IIO_COMBO_SIGNAL "changed"

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds5_scale_hid)
			dds5_scale_hid = g_signal_connect(dds5_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds5_freq_hid)
			dds5_freq_hid = g_signal_connect(dds5_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);

		if (!dds1_phase_hid)
			dds1_phase_hid = g_signal_connect(dds1_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds5_phase_hid)
			dds5_phase_hid = g_signal_connect(dds5_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);

		/* Disconnect the rest (2 & 6) */
		if (dds2_scale_hid) {
			g_signal_handler_disconnect(dds2_scale, dds2_scale_hid);
			dds2_scale_hid = 0;
		}
		if (dds6_scale_hid) {
			g_signal_handler_disconnect(dds6_scale, dds6_scale_hid);
			dds6_scale_hid = 0;
		}

		if (dds2_freq_hid) {
			g_signal_handler_disconnect(dds2_freq, dds2_freq_hid);
			dds2_freq_hid = 0;
		}
		if (dds6_freq_hid) {
			g_signal_handler_disconnect(dds6_freq, dds6_freq_hid);
			dds6_freq_hid = 0;
		}

		if (dds2_phase_hid) {
			g_signal_handler_disconnect(dds2_phase, dds2_phase_hid);
			dds2_phase_hid = 0;
		}
		if (dds6_phase_hid) {
			g_signal_handler_disconnect(dds6_phase, dds6_phase_hid);
			dds6_phase_hid = 0;
		}

		dds_locked_scale_cb(NULL, NULL);
		dds_locked_freq_cb(NULL, NULL);
		dds_locked_phase_cb(NULL, NULL);
		break;
	case 2:
		/* Two tones */
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX1_l),"<b>Two Tones</b>");
		gtk_label_set_markup(GTK_LABEL(dds_I_TX2_l),"<b>Two Tones</b>");
		gtk_widget_show(dds1_freq);
		gtk_widget_show(dds2_freq);
		gtk_widget_hide(dds3_freq);
		gtk_widget_hide(dds4_freq);
		gtk_widget_show(dds5_freq);
		gtk_widget_show(dds6_freq);
		gtk_widget_hide(dds7_freq);
		gtk_widget_hide(dds8_freq);
		gtk_widget_show(dds1_scale);
		gtk_widget_show(dds2_scale);
		gtk_widget_hide(dds3_scale);
		gtk_widget_hide(dds4_scale);
		gtk_widget_show(dds5_scale);
		gtk_widget_show(dds6_scale);
		gtk_widget_hide(dds7_scale);
		gtk_widget_hide(dds8_scale);
		gtk_widget_show(dds1_phase);
		gtk_widget_show(dds2_phase);
		gtk_widget_hide(dds3_phase);
		gtk_widget_hide(dds4_phase);
		gtk_widget_show(dds5_phase);
		gtk_widget_show(dds6_phase);
		gtk_widget_hide(dds7_phase);
		gtk_widget_hide(dds8_phase);
		gtk_widget_show(dds1_freq_l);
		gtk_widget_show(dds2_freq_l);
		gtk_widget_hide(dds3_freq_l);
		gtk_widget_hide(dds4_freq_l);
		gtk_widget_show(dds5_freq_l);
		gtk_widget_show(dds6_freq_l);
		gtk_widget_hide(dds7_freq_l);
		gtk_widget_hide(dds8_freq_l);
		gtk_widget_show(dds1_scale_l);
		gtk_widget_show(dds2_scale_l);
		gtk_widget_hide(dds3_scale_l);
		gtk_widget_hide(dds4_scale_l);
		gtk_widget_show(dds5_scale_l);
		gtk_widget_show(dds6_scale_l);
		gtk_widget_hide(dds7_scale_l);
		gtk_widget_hide(dds8_scale_l);
		gtk_widget_show(dds1_phase_l);
		gtk_widget_show(dds2_phase_l);
		gtk_widget_hide(dds3_phase_l);
		gtk_widget_hide(dds4_phase_l);
		gtk_widget_show(dds5_phase_l);
		gtk_widget_show(dds6_phase_l);
		gtk_widget_hide(dds7_phase_l);
		gtk_widget_hide(dds8_phase_l);
		gtk_widget_show(dds_I_TX1_l);
		gtk_widget_show(dds_I1_TX1_l);
		gtk_widget_show(dds_I2_TX1_l);
		gtk_widget_show(dds_I_TX2_l);
		gtk_widget_show(dds_I1_TX2_l);
		gtk_widget_show(dds_I2_TX2_l);
		gtk_widget_hide(dds_Q_TX1_l);
		gtk_widget_hide(dds_Q1_TX1_l);
		gtk_widget_hide(dds_Q2_TX1_l);
		gtk_widget_hide(dds_Q_TX2_l);
		gtk_widget_hide(dds_Q1_TX2_l);
		gtk_widget_hide(dds_Q2_TX2_l);

		if (!dds1_scale_hid)
			dds1_scale_hid = g_signal_connect(dds1_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds2_scale_hid)
			dds2_scale_hid = g_signal_connect(dds2_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds5_scale_hid)
			dds5_scale_hid = g_signal_connect(dds5_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);
		if (!dds6_scale_hid)
			dds6_scale_hid = g_signal_connect(dds6_scale , IIO_COMBO_SIGNAL,
					G_CALLBACK(dds_locked_scale_cb), NULL);

		if (!dds1_freq_hid)
			dds1_freq_hid = g_signal_connect(dds1_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds2_freq_hid)
			dds2_freq_hid = g_signal_connect(dds2_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds5_freq_hid)
			dds5_freq_hid = g_signal_connect(dds5_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds6_freq_hid)
			dds6_freq_hid = g_signal_connect(dds6_freq , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_freq_cb), NULL);
		if (!dds1_phase_hid)
			dds1_phase_hid = g_signal_connect(dds1_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds2_phase_hid)
			dds2_phase_hid = g_signal_connect(dds2_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds5_phase_hid)
			dds5_phase_hid = g_signal_connect(dds5_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);
		if (!dds6_phase_hid)
			dds6_phase_hid = g_signal_connect(dds6_phase , IIO_SPIN_SIGNAL,
					G_CALLBACK(dds_locked_phase_cb), NULL);

		/* Force sync */
		dds_locked_scale_cb(NULL, NULL);
		dds_locked_freq_cb(NULL, NULL);
		dds_locked_phase_cb(NULL, NULL);

		break;
	case 3:
		/* Independant/Individual control */
		enable_dds(true);
		gtk_widget_hide(dac_buffer);
		gtk_label_set_markup(GTK_LABEL(dds_I_TX1_l),"<b>Channel I</b>");
		gtk_label_set_markup(GTK_LABEL(dds_I_TX2_l),"<b>Channel I</b>");
		show_all_I_and_Q();

		if (dds1_scale_hid) {
			g_signal_handler_disconnect(dds1_scale, dds1_scale_hid);
			dds1_scale_hid = 0;
		}
		if (dds2_scale_hid) {
			g_signal_handler_disconnect(dds2_scale, dds2_scale_hid);
			dds2_scale_hid = 0;
		}
		if (dds5_scale_hid) {
			g_signal_handler_disconnect(dds5_scale, dds5_scale_hid);
			dds5_scale_hid = 0;
		}
		if (dds6_scale_hid) {
			g_signal_handler_disconnect(dds6_scale, dds6_scale_hid);
			dds6_scale_hid = 0;
		}

		if (dds1_freq_hid) {
			g_signal_handler_disconnect(dds1_freq, dds1_freq_hid);
			dds1_freq_hid = 0;
		}
		if (dds2_freq_hid) {
			g_signal_handler_disconnect(dds2_freq, dds2_freq_hid);
			dds2_freq_hid = 0;
		}
		if (dds5_freq_hid) {
			g_signal_handler_disconnect(dds5_freq, dds5_freq_hid);
			dds5_freq_hid = 0;
		}
		if (dds6_freq_hid) {
			g_signal_handler_disconnect(dds6_freq, dds6_freq_hid);
			dds6_freq_hid = 0;
		}

		if (dds1_phase_hid) {
			g_signal_handler_disconnect(dds1_phase, dds1_phase_hid);
			dds1_phase_hid = 0;
		}

		if (dds2_phase_hid) {
			g_signal_handler_disconnect(dds2_phase, dds2_phase_hid);
			dds2_phase_hid = 0;
		}
		if (dds5_phase_hid) {
			g_signal_handler_disconnect(dds5_phase, dds5_phase_hid);
			dds5_phase_hid = 0;
		}

		if (dds6_phase_hid) {
			g_signal_handler_disconnect(dds6_phase, dds6_phase_hid);
			dds6_phase_hid = 0;
		}

		break;
	case 4:
		/* Buffer */
		gtk_widget_show(dac_buffer);
		enable_dds(false);
		hide_all_I_and_Q();
		break;
	default:
		printf("glade file out of sync with C file - please contact developers\n");
		break;
	}
}

/* Check for a valid two channels combination (ch0->ch1, ch2->ch3, ...)
 *
 * struct iio_channel_info *chanels - list of channels of a device
 * int ch_count - number of channel in the list
 * char* ch_name - output parameter: stores references to the enabled
 *                 channels.
 * Return 1 if the channel combination is valid
 * Return 0 if the combination is not valid
 */
int channel_combination_check(struct iio_device *dev, const char **ch_names)
{
	bool consecutive_ch = FALSE;
	unsigned int i, k, nb_channels = iio_device_get_channels_count(dev);
	struct extra_info *prev_info = NULL;

	for (i = 0, k = 0; i < nb_channels; i++) {
		struct iio_channel *ch = iio_device_get_channel(dev, i);
		struct extra_info *info = iio_channel_get_data(ch);

		if (info->may_be_enabled) {
			const char *name = iio_channel_get_name(ch) ?: iio_channel_get_id(ch);
			ch_names[k++] = name;

			if (i > 0) {
				struct extra_info *prev = iio_channel_get_data(iio_device_get_channel(dev, i - 1));
				if (prev->may_be_enabled) {
					consecutive_ch = TRUE;
					break;
				}
			}
		}
	}
	if (!consecutive_ch)
		return 0;

	if (!(i & 0x1))
		return 0;

	return 1;
}

static int fmcomms2_init(GtkWidget *notebook)
{
	GtkBuilder *builder;
	GtkWidget *fmcomms2_panel;
	bool shared_scale_available;

	builder = gtk_builder_new();
	nbook = GTK_NOTEBOOK(notebook);

	if (!gtk_builder_add_from_file(builder, "fmcomms2.glade", NULL))
		gtk_builder_add_from_file(builder, OSC_GLADE_FILE_PATH "fmcomms2.glade", NULL);

	is_2rx_2tx = iio_devattr_exists("ad9361-phy", "in_voltage1_hardwaregain");

	fmcomms2_panel = GTK_WIDGET(gtk_builder_get_object(builder, "fmcomms2_panel"));
	/* Global settings */
	ensm_mode = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode"));
	ensm_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "ensm_mode_available"));
	calib_mode = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode"));
	calib_mode_available = GTK_WIDGET(gtk_builder_get_object(builder, "calib_mode_available"));
	trx_rate_governor = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor"));
	trx_rate_governor_available = GTK_WIDGET(gtk_builder_get_object(builder, "trx_rate_governor_available"));
	tx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_tx_path"));
	rx_path_rates = GTK_WIDGET(gtk_builder_get_object(builder, "label_rx_path"));
	filter_fir_config = GTK_WIDGET(gtk_builder_get_object(builder, "filter_fir_config"));

	/* Receive Chain */

	rf_port_select_rx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_rx"));
	rx_gain_control_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx1"));
	rx_gain_control_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_rx2"));
	rx_gain_control_modes_rx1 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx1"));
	rx_gain_control_modes_rx2 = GTK_WIDGET(gtk_builder_get_object(builder, "gain_control_mode_available_rx2"));
	rx1_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx1"));
	rx2_rssi = GTK_WIDGET(gtk_builder_get_object(builder, "rssi_rx2"));
	enable_fir_filter_rx = GTK_WIDGET(gtk_builder_get_object(builder, "enable_fir_filter_rx"));
	rx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "rx_fastlock_profile"));
	/* Transmit Chain */
	rf_port_select_tx = GTK_WIDGET(gtk_builder_get_object(builder, "rf_port_select_tx"));
	fir_filter_en_tx = GTK_WIDGET(gtk_builder_get_object(builder, "fir_filter_en_tx"));
	tx_fastlock_profile = GTK_WIDGET(gtk_builder_get_object(builder, "tx_fastlock_profile"));
	dds_mode = GTK_WIDGET(gtk_builder_get_object(builder, "dds_mode"));

	dds1_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_freq"));
	dds2_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_freq"));
	dds3_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_freq"));
	dds4_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_freq"));
	dds5_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_freq"));
	dds6_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_freq"));
	dds7_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_freq"));
	dds8_freq = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_freq"));

	dds1_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_scale"));
	dds2_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_scale"));
	dds3_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_scale"));
	dds4_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_scale"));
	dds5_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_scale"));
	dds6_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_scale"));
	dds7_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_scale"));
	dds8_scale = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_scale"));

	dds1_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_phase"));
	dds2_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_phase"));
	dds3_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_phase"));
	dds4_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_phase"));
	dds5_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_phase"));
	dds6_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_phase"));
	dds7_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_phase"));
	dds8_phase = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_phase"));

	adj1_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_I1_freq"));
	adj2_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_I2_freq"));
	adj3_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_Q1_freq"));
	adj4_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX1_Q1_freq"));
	adj5_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_I1_freq"));
	adj6_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_I2_freq"));
	adj7_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_Q1_freq"));
	adj8_freq = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "adjustment_TX2_Q2_freq"));

	dds1_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_freq_txt"));
	dds2_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_freq_txt"));
	dds3_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_freq_txt"));
	dds4_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_freq_txt"));
	dds5_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_freq_txt"));
	dds6_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_freq_txt"));
	dds7_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_freq_txt"));
	dds8_freq_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_freq_txt"));

	dds1_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_scale_txt"));
	dds2_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_scale_txt"));
	dds3_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_scale_txt"));
	dds4_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_scale_txt"));
	dds5_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_scale_txt"));
	dds6_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_scale_txt"));
	dds7_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_scale_txt"));
	dds8_scale_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_scale_txt"));

	dds1_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx1_phase_txt"));
	dds2_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx1_phase_txt"));
	dds3_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx1_phase_txt"));
	dds4_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx1_phase_txt"));
	dds5_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_tx2_phase_txt"));
	dds6_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_tx2_phase_txt"));
	dds7_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_tx2_phase_txt"));
	dds8_phase_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_tx2_phase_txt"));

	dds_I_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_I_TX1_l"));
	dds_I1_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_TX1_l"));
	dds_I2_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_TX1_l"));
	dds_I_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_I_TX2_l"));
	dds_I1_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I1_TX2_l"));
	dds_I2_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_I2_TX2_l"));
	dds_Q_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_Q_TX1_l"));
	dds_Q1_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_TX1_l"));
	dds_Q2_TX1_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_TX1_l"));
	dds_Q_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_Q_TX2_l"));
	dds_Q1_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q1_TX2_l"));
	dds_Q2_TX2_l = GTK_WIDGET(gtk_builder_get_object(builder, "dds_tone_Q2_TX2_l"));
	dac_buffer = GTK_WIDGET(gtk_builder_get_object(builder, "dac_buffer"));

	gtk_combo_box_set_active(GTK_COMBO_BOX(ensm_mode_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(trx_rate_governor_available), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx1), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_gain_control_modes_rx2), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), 1);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_rx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rf_port_select_tx), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(rx_fastlock_profile), 0);
	gtk_combo_box_set_active(GTK_COMBO_BOX(tx_fastlock_profile), 0);

	/* Bind the IIO device files to the GUI widgets */

	/* Global settings */
	iio_combo_box_init(&glb_widgets[num_glb++],
		"ad9361-phy", "ensm_mode", "ensm_mode_available",
		ensm_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		"ad9361-phy", "calib_mode", "calib_mode_available",
		calib_mode_available, NULL);
	iio_combo_box_init(&glb_widgets[num_glb++],
		"ad9361-phy", "trx_rate_governor", "trx_rate_governor_available",
		trx_rate_governor_available, NULL);

	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		"ad9361-phy", "dcxo_tune_coarse", builder, "dcxo_coarse_tune",
		0);
	iio_spin_button_int_init_from_builder(&glb_widgets[num_glb++],
		"ad9361-phy", "dcxo_tune_fine", builder, "dcxo_fine_tune",
		0);

	/* Receive Chain */


	iio_combo_box_init(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_gain_control_mode",
		"in_voltage_gain_control_mode_available",
		rx_gain_control_modes_rx1, NULL);

	iio_combo_box_init(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_rf_port_select",
		"in_voltage_rf_port_select_available",
		rf_port_select_rx, NULL);

	if (is_2rx_2tx)
		iio_combo_box_init(&rx_widgets[num_rx++],
			"ad9361-phy", "in_voltage1_gain_control_mode",
			"in_voltage_gain_control_mode_available",
			rx_gain_control_modes_rx2, NULL);
	rx1_gain = num_rx;
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage0_hardwaregain", builder,
		"hardware_gain_rx1", NULL);

	if (is_2rx_2tx) {
		rx2_gain = num_rx;
		iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
			"ad9361-phy", "in_voltage1_hardwaregain", builder,
			"hardware_gain_rx2", NULL);
	}
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_sampling_frequency", builder,
		"sampling_freq_rx", &mhz_scale);
	iio_spin_button_int_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_rf_bandwidth", builder, "rf_bandwidth_rx",
		&mhz_scale);
	rx_lo = num_rx;
	iio_spin_button_s64_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "out_altvoltage0_RX_LO_frequency", builder,
		"rx_lo_freq", &mhz_scale);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_quadrature_tracking_en", builder,
		"quad", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_rf_dc_offset_tracking_en", builder,
		"rfdc", 0);
	iio_toggle_button_init_from_builder(&rx_widgets[num_rx++],
		"ad9361-phy", "in_voltage_bb_dc_offset_tracking_en", builder,
		"bbdc", 0);

	/* Transmit Chain */

	iio_combo_box_init(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage0_rf_port_select",
		"out_voltage_rf_port_select_available",
		rf_port_select_tx, NULL);

	iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage0_hardwaregain", builder,
		"hardware_gain_tx1", &inv_scale);

	if (is_2rx_2tx)
		iio_spin_button_init_from_builder(&tx_widgets[num_tx++],
			"ad9361-phy", "out_voltage1_hardwaregain", builder,
			"hardware_gain_tx2", &inv_scale);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage_sampling_frequency", builder,
		"sampling_freq_tx", &mhz_scale);
	iio_spin_button_int_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_voltage_rf_bandwidth", builder,
		"rf_bandwidth_tx", &mhz_scale);

	tx_lo = num_tx;
	iio_spin_button_s64_init_from_builder(&tx_widgets[num_tx++],
		"ad9361-phy", "out_altvoltage1_TX_LO_frequency", builder,
		"tx_lo_freq", &mhz_scale);

	shared_scale_available = iio_devattr_exists("cf-ad9361-dds-core-lpc",
			"out_altvoltage_scale_available");

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_frequency",
			dds1_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_frequency",
			dds2_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_frequency",
			dds3_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_frequency",
			dds4_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_frequency",
			dds5_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_frequency",
			dds6_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_frequency",
			dds7_freq, &mhz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_frequency",
			dds8_freq, &mhz_scale);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_I_F1_scale_available",
			dds1_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_I_F2_scale_available",
			dds2_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_Q_F1_scale_available",
			dds3_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX1_Q_F2_scale_available",
			dds4_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_I_F1_scale_available",
			dds5_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_I_F2_scale_available",
			dds6_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_Q_F1_scale_available",
			dds7_scale, compare_gain);
	iio_combo_box_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_scale",
			shared_scale_available ?
				"out_altvoltage_scale_available" :
				"out_altvoltage_TX2_Q_F2_scale_available",
				dds8_scale, compare_gain);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage0_TX1_I_F1_phase",
			dds1_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage1_TX1_I_F2_phase",
			dds2_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage2_TX1_Q_F1_phase",
			dds3_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage3_TX1_Q_F2_phase",
			dds4_phase, &khz_scale);

	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage4_TX2_I_F1_phase",
			dds5_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage5_TX2_I_F2_phase",
			dds6_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage6_TX2_Q_F1_phase",
			dds7_phase, &khz_scale);
	iio_spin_button_init(&tx_widgets[num_tx++],
			"cf-ad9361-dds-core-lpc", "out_altvoltage7_TX2_Q_F2_phase",
			dds8_phase, &khz_scale);

	manage_dds_mode();

	/* Signals connect */
	g_signal_connect(dds_mode, "changed", G_CALLBACK(manage_dds_mode),
		NULL);
	g_builder_connect_signal(builder, "fmcomms2_settings_save", "clicked",
		G_CALLBACK(save_button_clicked), NULL);

	g_builder_connect_signal(builder, "fmcomms2_settings_reload", "clicked",
		G_CALLBACK(reload_button_clicked), NULL);

	g_builder_connect_signal(builder, "filter_fir_config", "file-set",
		G_CALLBACK(filter_fir_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "dac_buffer", "file-set",
		G_CALLBACK(dac_buffer_config_file_set_cb), NULL);

	g_builder_connect_signal(builder, "rx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 1);
	g_builder_connect_signal(builder, "tx_fastlock_store", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 2);
	g_builder_connect_signal(builder, "rx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 3);
	g_builder_connect_signal(builder, "tx_fastlock_recall", "clicked",
		G_CALLBACK(fastlock_clicked), (gpointer) 4);

	g_builder_connect_signal(builder, "global_settings_toggle", "clicked",
		G_CALLBACK(hide_section_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "global_settings")));

	g_builder_connect_signal(builder, "tx_toggle", "clicked",
		G_CALLBACK(hide_section_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "tx_settings")));

	g_builder_connect_signal(builder, "rx_toggle", "clicked",
		G_CALLBACK(hide_section_cb),
		GTK_WIDGET(gtk_builder_get_object(builder, "rx_settings")));

	iio_update_widgets(glb_widgets, num_glb);
	tx_update_values();
	rx_update_values();

	glb_settings_update_labels();
	rssi_update_labels();

	add_ch_setup_check_fct("cf-ad9361-lpc", channel_combination_check);
	plugin_fft_corr = 20 * log10(1/sqrt(HANNING_ENBW));

	block_diagram_init(builder, "fmcomms2.svg");

	this_page = gtk_notebook_append_page(GTK_NOTEBOOK(notebook), fmcomms2_panel, NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(notebook), fmcomms2_panel, "FMComms2");
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(filter_fir_config), OSC_FILTER_FILE_PATH);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(dac_buffer), OSC_WAVEFORM_FILE_PATH);

	if (!is_2rx_2tx) {
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame7")));
		gtk_widget_hide(GTK_WIDGET(gtk_builder_get_object(builder, "frame10")));
	}

	g_thread_new("Update_thread", (void *) &update_display, NULL);

	return 0;
}

#define SYNC_RELOAD "SYNC_RELOAD"

static char *handle_item(struct osc_plugin *plugin, const char *attrib,
			 const char *value)
{
	char *buf;

	if (MATCH_ATTRIB(SYNC_RELOAD)) {
		if (value)
			reload_button_clicked(NULL, 0);
		else
			return "1";
	} else if (MATCH_ATTRIB("load_fir_filter_file")) {
		if (value) {
			if (value[0])
				load_fir_filter(value);
		} else {
			return last_fir_filter;
		}
	} else if (MATCH_ATTRIB("dds_mode")) {
		if (value) {
			gtk_combo_box_set_active(GTK_COMBO_BOX(dds_mode), atoi(value));
		} else {
			buf = malloc (10);
			sprintf(buf, "%i", gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)));
			return buf;
		}
	} else if (MATCH_ATTRIB("dac_buf_filename") &&
				gtk_combo_box_get_active(GTK_COMBO_BOX(dds_mode)) == 4) {
		if (value) {
			gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dac_buffer), value);
			process_dac_buffer_file(value);
		} else
			return dac_buf_filename;
	} else {
		if (value) {
			printf("Unhandled tokens in ini file,\n"
				"\tSection %s\n\tAtttribute : %s\n\tValue: %s\n",
				"FMComms2", attrib, value);
			return "FAIL";
		}
	}

	return NULL;
}

static const char *fmcomms2_sr_attribs[] = {
	"ad9361-phy.trx_rate_governor",
	"ad9361-phy.dcxo_tune_coarse",
	"ad9361-phy.dcxo_tune_fine",
	"ad9361-phy.ensm_mode",
	"ad9361-phy.in_voltage0_rf_port_select",
	"ad9361-phy.in_voltage0_gain_control_mode",
	"ad9361-phy.in_voltage0_hardwaregain",
	"ad9361-phy.in_voltage1_gain_control_mode",
	"ad9361-phy.in_voltage1_hardwaregain",
	"ad9361-phy.in_voltage_bb_dc_offset_tracking_en",
	"ad9361-phy.in_voltage_quadrature_tracking_en",
	"ad9361-phy.in_voltage_rf_dc_offset_tracking_en",
	"ad9361-phy.out_voltage0_rf_port_select",
	"ad9361-phy.out_altvoltage0_RX_LO_frequency",
	"ad9361-phy.out_altvoltage1_TX_LO_frequency",
	"ad9361-phy.out_voltage0_hardwaregain",
	"ad9361-phy.out_voltage1_hardwaregain",
	"ad9361-phy.out_voltage_sampling_frequency",
	"ad9361-phy.in_voltage_rf_bandwidth",
	"ad9361-phy.out_voltage_rf_bandwidth",
	"load_fir_filter_file",
	"ad9361-phy.in_voltage_filter_fir_en",
	"ad9361-phy.out_voltage_filter_fir_en",
	"ad9361-phy.in_out_voltage_filter_fir_en",
	"dds_mode",
	"dac_buf_filename",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage0_TX1_I_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage1_TX1_I_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage2_TX1_Q_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage3_TX1_Q_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage4_TX2_I_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage5_TX2_I_F2_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage6_TX2_Q_F1_scale",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_frequency",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_phase",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_raw",
	"cf-ad9361-dds-core-lpc.out_altvoltage7_TX2_Q_F2_scale",
	SYNC_RELOAD,
	NULL,
};

static void update_active_page(gint active_page, gboolean is_detached)
{
	this_page = active_page;
	plugin_detached = is_detached;
}

static bool fmcomms2_identify(void)
{
	return !set_dev_paths("ad9361-phy");
}

struct osc_plugin plugin = {
	.name = "FMComms2/3",
	.identify = fmcomms2_identify,
	.init = fmcomms2_init,
	.save_restore_attribs = fmcomms2_sr_attribs,
	.handle_item = handle_item,
	.update_active_page = update_active_page,
};
