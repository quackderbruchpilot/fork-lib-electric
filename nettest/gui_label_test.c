/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <acfutils/crc64.h>
#include <acfutils/log.h>
#include <acfutils/cairo_utils.h>

/* Capture actual renderer text, while still drawing through Cairo. */
static const char *expected_text;
static unsigned text_matches;
static void
test_log(const char *text)
{
	fputs(text, stderr);
}

static void
capture_text(cairo_t *cr, const char *text)
{
	if (expected_text != NULL && strcmp(text, expected_text) == 0)
		text_matches++;
	cairo_show_text(cr, text);
}

#define cairo_show_text capture_text
#include "../src/libelec_drawing.c"
#undef cairo_show_text

static char *
read_file(const char *filename)
{
	FILE *fp;
	char *text;
	long len;

	fp = fopen(filename, "rb");
	VERIFY(fp != NULL);
	VERIFY(fseek(fp, 0, SEEK_END) == 0);
	len = ftell(fp);
	VERIFY(len >= 0);
	VERIFY(fseek(fp, 0, SEEK_SET) == 0);
	text = malloc((size_t)len + 1);
	VERIFY(text != NULL);
	VERIFY(fread(text, 1, (size_t)len, fp) == (size_t)len);
	text[len] = '\0';
	VERIFY(fclose(fp) == 0);

	return (text);
}

static elec_sys_t *
load_text(const char *text)
{
	FILE *fp = fopen("gui_label_test_generated.net", "wb");
	VERIFY(fp != NULL);
	VERIFY(fwrite(text, 1, strlen(text), fp) == strlen(text));
	VERIFY(fclose(fp) == 0);
	return (libelec_new("gui_label_test_generated.net"));
}

static void
expect_bad(const char *text)
{
	elec_sys_t *sys = load_text(text);
	VERIFY(sys == NULL);
}

int
main(void)
{
	elec_sys_t *sys;
	elec_comp_t *batt, *bus, *cb;
	cairo_surface_t *surface;
	cairo_t *cr;
	elec_comp_info_t legacy = { 0 };
	char *label, *bad, *network;
	char long_label[1024];

	log_init(test_log, "gui_label_test");
	crc64_init();
	network = read_file(GUI_LABEL_TEST_NETWORK);
	sys = libelec_new(GUI_LABEL_TEST_NETWORK);
	VERIFY(sys != NULL);
	batt = libelec_comp_find(sys, "2PB1");
	bus = libelec_comp_find(sys, "3PP");
	cb = libelec_comp_find(sys, "4PB1");
	VERIFY(batt != NULL && bus != NULL && cb != NULL);
	VERIFY(strcmp(libelec_comp_get_name(batt), "2PB1") == 0);
	VERIFY(strcmp(batt->info->gui.label, "Battery 1") == 0);
	VERIFY(strcmp(libelec_comp_find(sys, "9PP1")->info->gui.label,
	    "Battery 1") == 0); /* Repeated labels are allowed. */
	VERIFY(libelec_comp_find(sys, "Battery 1") == NULL);
	VERIFY(libelec_comp_find(sys, "2PB1_Battery_1") == NULL);
	VERIFY(libelec_comp_find(sys, "9PP2")->info->gui.label == NULL);
	VERIFY(libelec_comp_get_num_conns(bus) == 3);

	surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1200, 800);
	cr = cairo_create(surface);
	cairo_translate(cr, 400, 300);
	expected_text = "2PB1 (Battery 1)";
	text_matches = 0;
	libelec_draw_layout(sys, cr, 16, 14);
	VERIFY(text_matches == 1);
	expected_text = "4PB1 (Battery breaker)";
	text_matches = 0;
	draw_bus_info(bus, cr, 16, 14, VECT2(0, 0));
	VERIFY(text_matches == 1);
	expected_text = "Powered by: 2PB1 (Battery 1)";
	text_matches = 0;
	/* Set only the test's source snapshot; no simulation thread needed. */
	cb->srcs_ext[0] = batt;
	draw_comp_info(cb, cr, 16, 14, VECT2(0, 0));
	VERIFY(text_matches == 1);
	VERIFY(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
	libelec_destroy(sys);

	legacy.name = "CB_LEGACY_O_P";
	label = make_comp_label(&legacy);
	VERIFY(strcmp(label, "LEGACY O/P") == 0);
	free(label);
	legacy.gui.label = "Literal_under_score 100% (aux)";
	label = make_comp_label(&legacy);
	VERIFY(strcmp(label,
	    "CB_LEGACY_O_P (Literal_under_score 100% (aux))") == 0);
	free(label);
	memset(long_label, 'x', sizeof (long_label) - 1);
	long_label[sizeof (long_label) - 1] = '\0';
	legacy.gui.label = long_label;
	label = make_comp_label(&legacy);
	VERIFY(strlen(label) == strlen(legacy.name) + strlen(long_label) + 3);
	free(label);
	bad = sprintf_alloc("LOAD 9PP1\n GUI_LABEL %s\n"
	    "BUS 3PP DC\n ENDPT 9PP1\n", long_label);
	sys = load_text(bad);
	VERIFY(sys != NULL);
	VERIFY(strcmp(libelec_comp_find(sys, "9PP1")->info->gui.label,
	    long_label) == 0);
	libelec_destroy(sys);
	free(bad);
	sys = load_text("LOAD 9PP1\n"
	    " GUI_LABEL   APU_BAT   100% (aux) # comment\n"
	    "BUS 3PP DC\n ENDPT 9PP1\n");
	VERIFY(sys != NULL);
	VERIFY(strcmp(libelec_comp_find(sys, "9PP1")->info->gui.label,
	    "APU_BAT 100% (aux)") == 0);
	libelec_destroy(sys);

	/* The real parser must reject duplicate BMKs even with distinct labels. */
	bad = sprintf_alloc("%sLOAD 2PB1\n GUI_LABEL Different name\n", network);
	expect_bad(bad);
	free(bad);
	expect_bad("LOAD 9PP1\n GUI_LABEL\n");
	expect_bad("GUI_LABEL Orphan\nLOAD 9PP1\n");
	expect_bad("LOAD 9PP1\n GUI_LABEL First\n GUI_LABEL Second\n");
	expect_bad("LABEL_BOX 0 0 1 1 1 Box\n GUI_LABEL Invalid\n");
	/* Fail after allocating a label and bus links: exercises error cleanup. */
	bad = sprintf_alloc("%s UNKNOWN invalid\n", network);
	expect_bad(bad);
	free(bad);
	free(network);
	VERIFY(remove("gui_label_test_generated.net") == 0);
	log_fini();
	puts("GUI_LABEL parser, identity, rendering and compatibility tests passed");
	return (0);
}
