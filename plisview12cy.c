//to execute ./plisview12cy -o FILENAME_TO_SAVE_SPK -t TIME -r RANGE -d DELAY -g GOODNESS_LIM (=0 write all signals, =2 write if 2 pairs of coinc, =3 do not write signals)
//18.10.2016 Program works properly only in coinc mode!!!

#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cyusb.h>
#include <getopt.h>
#include <string.h>
#include <pthread.h>
#include "plot.h"

#include <sys/time.h>


#define FREE_DATA(to) do { for (j = 0; j < to; j++) { free(data[j]); data[j] = NULL; } } while(0);
#define FREE_FILENAME_TO_SAVE_STR() do { free(files.name_to_save); files.name_to_save = NULL; } while(0);
	      

#define TIMES_TO_WRITE_SPK  (100000) //(3000)


static cyusb_handle *h = NULL;
int send_command(const char command, int args);
void calc_results(GtkTextBuffer * txtbuffer, int *a, int *b);

char FILETYPE = 1; // 1 for Max spks, 0 for Serg spks 
GtkWidget *main_window, *main_statusbar, *coord_statusbar, *entry_range[4], *button_set_range[4], *entry_delay, *label_intens;
static int times_started = 0, intens = 0;
long int times_asked = 0;

int inFile = 0, ok_read = 1;
char *name_to_read, *name_to_save;

pthread_t tid1, tid2;
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;
int *data[4];
double start[2];

int acq_time = 20;
int rangeset[4] = {90, 90, 90, 90};
int delay = 100;
unsigned char odd_start_draw = 0;
int goodness_lim = 0;

double T_SCALE[2] = {100.0, 2.0}; //for 60Co and 181Hf 100, 20; for 44Ti 100, 100; for Razv 100, 2.0
//int EN_RANGE[4][4] = {{500, 650, 2000, 2200}, {480, 600, 1870, 2070}, {511, 606, 1951, 2150}, {520, 630, 1980, 2180}}; //181Hf
//int EN_RANGE[4][4] = {{2044, 2196, 2248, 2534}, {1886, 2089, 2117, 2404}, {2088, 2254, 2310, 2651}, {1975, 2147, 2184, 2462}}; //60Co
//int EN_RANGE[4][4] = {{851, 973, 851, 973}, {800, 890, 800, 890}, {830, 1055, 830, 1055}, {828, 900, 828, 900}}; //44Ti PAC
//int EN_RANGE[4][4] = {{873, 980, 2002, 2162}, {789, 927, 1837, 2032}, {891, 1057, 2010, 2311}, {801, 997, 1878, 2152}}; //44Ti lifetime
//int EN_RANGE[4][4] = {{851, 973, 851, 973}, {2577, 2753, 2865, 3010}, {2865, 3010, 2577, 2753}, {1650, 2150, 1650, 2150}}; //44Ti + filter
int EN_RANGE[4][4] = {{851, 973, 851, 973}, {1572, 1684, 1728, 1852}, {1903, 2020, 2056, 2228}, {1650, 2150, 1650, 2150}};

cairo_surface_t *surface1 = NULL;

static struct {
    char *name_to_read;
    int fd_read;
    FILE *fd_Fread;
    int readpos;
    gint sb_context_id;
	
    char *name_to_save;
} files;

int f_meander()
{
	static int i = 0;
	static int width = 60, height = 10;
	
	i++;
	if (i/width == 0) return 0;
	else {
		if (i == 2*width-1) i = 0;
		return height;
	}
}

int f_sin()
{
	static int i = 0;
	double T = 10.0;
	i += 5;
	
	return (int)sin(2.0*M_PI*i/T);
}

extern int max_bubble(int *x, int nums)
{
	int i, z;
	
	for (i = 1, z = x[0]; i<=nums-1; i++)
		if(x[i] > z)
			z = x[i];
	
	return z;
}

extern int min_bubble(int *x, int nums)
{
	int i, z;
	
	for (i = 1, z = x[0]; i<=nums-1; i++)
		if (x[i] < z)
			z = x[i];
	
	return z;
}

extern int min_bubble_num(int *x, int nums)
{
	int i, z;
	int max_num;
	
	for (i = 1, z = x[0]; i<=nums-1; i++)
		if (x[i] < z) {
			z = x[i];
			max_num = i;
		}
	
	return max_num;
}

void swap(int *a, int *b) 
{
	int x = *b;
	
	*b = *a;
	*a = x;
}

int definitely_greater_than(double a, double b) 
{
    return (b-a) > ( (fabs(a) < fabs(b) ? fabs(b) : fabs(a)) * EPS );
}


void clicked_on_ga(GtkWidget *widget, GdkEventButton *event, gpointer   data)
{	
	global_fl.zoom_in = 1;
	
	if (event->button == 3) {
		global_fl.zoom_in = 0;
		gtk_widget_queue_draw(main_window);
	}
		
	printf("clicked, zoomin = %d, %d\n", global_fl.zoom_in, (event->state & GDK_BUTTON1_MASK));
}

void unclicked_on_ga(GtkWidget *widget, GdkEventButton *event, gpointer   data)
{
	if ((event->state & GDK_BUTTON1_MASK) && (global_fl.zoom_in == 2)) {
		global_fl.zoom_in = 3;
		gtk_widget_queue_draw(main_window);
	}
	
	printf("unclicked, zoomin = %d\n", global_fl.zoom_in);
}

static void motion_in_ga(GtkWidget *widget, GdkEventMotion *event, gpointer   user_data)
{	
    //printf("Motion on GA! (%.2f, %.2f) STATE = %d\n", event->x, event->y, (event->state & GDK_BUTTON2_MASK));
    GtkAllocation allocation;
    gchar *tempstr = NULL;
    
    gtk_widget_get_allocation (widget, &allocation);
    
    int min_i = (SIZEOF_DATA(FILETYPE)/2-scale_opt.x_range)/2 - scale_opt.x;
    int max_i = SIZEOF_DATA(FILETYPE)/2-1-(SIZEOF_DATA(FILETYPE)/2-scale_opt.x_range)/2 + scale_opt.x;
    gfloat x_range = (gfloat) (max_i-min_i-2.0*scale_opt.x);
	
    int real_i = (int)round( (event->x-2.0*X_NULL)*x_range/(allocation.width-2.0*X_NULL)+min_i );
	
    //printf("real i = %d, x = %.1f\n", real_i, event->x);
	
    if (real_i >= 0) {
        tempstr = g_strdup_printf("(%d, %d)", real_i, data[GPOINTER_TO_INT(user_data)][real_i]);
	gtk_statusbar_push(GTK_STATUSBAR(coord_statusbar), 0, tempstr);
        g_free(tempstr);
	tempstr = NULL;
    }
}

static void touch_ga(GtkWidget *widget, gpointer   data)
{
    printf("Touch GA!\n");
}

static gboolean graph_configure_event (GtkWidget         *widget,
                          GdkEventConfigure *event,
                          gpointer           data)
{
    printf("%s\n", __FUNCTION__);
	
    GtkAllocation allocation;

    if (surface1) {
	cairo_surface_destroy (surface1);
    }
    gtk_widget_get_allocation (widget, &allocation);
    surface1 = gdk_window_create_similar_surface (gtk_widget_get_window (widget),
						  CAIRO_CONTENT_COLOR,
						  allocation.width,
						  allocation.height/2);
}

int control_test(unsigned char bmRequestType) 
{
    int r;
    unsigned char datac[2] = {15, 15};
	
    //datac[0] = datac[1] = 15;
	
    r = cyusb_control_transfer(h, bmRequestType, 0x00, 0x0000, 0x0000, datac, 2, 0); //wIndex = 0x0000 or LIBUSB_ENDPOINT_IN | IN_EP 0b0000 0000 1000 xxxx
	
    return datac[0];
}


void *write_to_ep(void *user_data)
{	
	static char reset_start = 1;
	
	int x, i, r, transferred = 0;
	unsigned char *buf = (unsigned char *)malloc(2*SIZEOF_DATA(FILETYPE)*sizeof(unsigned char));
	
	if (!access(files.name_to_save, F_OK)) {
		remove(files.name_to_save);
		printf("File was removed\n");
	}
	
	if (!inFile) inFile = open (files.name_to_save, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	while (TRUE) {
		//printf("flag=%d\n", ok_read);
		
		if (ok_read) {
			//send_command('r', 0);
			
			for (i = 0; i<=SIZEOF_DATA(FILETYPE)-1; i++) {
					buf[2*i] = 2*f_meander();
					buf[2*i+1] = 4 & 0b00001111;
			}
			r = cyusb_bulk_transfer(h, OUT_EP, buf, 2*SIZEOF_DATA(FILETYPE), &transferred, TIMEOUT);
			
			if (r != 0 || transferred != 2*SIZEOF_DATA(FILETYPE)) {
				if(transferred != 2*SIZEOF_DATA(FILETYPE))
					printf("Error in bulk write transfer, transferred = %d \n", transferred);
				cyusb_error(r);
				cyusb_close();
				exit(0);
			}
			
			
			usleep(20000);
			//odd_start_draw++;
			
			//if(reset_start)
			//	send_command('r', 0);
			OneToZero(reset_start);
			
			r = cyusb_bulk_transfer(h, IN_EP, buf, 2*SIZEOF_DATA(FILETYPE), &transferred, TIMEOUT);
			if (r != 0 || transferred != 2*SIZEOF_DATA(FILETYPE)) {
				if(transferred != 2*SIZEOF_DATA(FILETYPE))
					printf("Error in bulk transfer, transferred = %d \n", transferred);
				cyusb_error(r);
				cyusb_close();
				exit(0);
			}
			printf("trans = %d\n", transferred);
			
			for (i = 0; i <= SIZEOF_DATA(FILETYPE)-1; i++) {
				data[reset_start][i] = buf[2*i]+256*(buf[2*i+1]&0b00111111);
			//	data[reset_start][i] = f_meander();
			//	printf("%u + 256*%u = %d\n", buf[2*i], buf[2*i+1], ((int *)data)[i]);
			}
			//printf("MAX in data = %d\n", max_bubble(data[reset_start], SIZEOF_DATA(FILETYPE)));
			
			for (i = 0; i < SIZEOF_DATA(FILETYPE)/2; i++) {
				swap(&(data[reset_start][i]), &(data[reset_start][SIZEOF_DATA(FILETYPE)-i-1]));
			}
			
		//printf("transferred = %d \n-------------------------------------------------------\n", transferred);
		
			if ( write(inFile, data[reset_start], sizeof(int)*SIZEOF_DATA(FILETYPE)) == -1 ) {
				perror("Error in read data!");
				exit(1);
			}
		}
	}
	
	free(buf);
	close(inFile);
	
	return 0;
}


int check_overflow_in_signal(const int *a)
{
    int i = 0;
    
    for (i = 10; i < 128; i++) {
        if (a[i] == 0) {
            return -1;
        }
    }
    
    return 0;
}


double area(int *a)
{
    int i;
    
    double baseline = 0.0;
    for (i = 0; i < 10; i++) {
         baseline += a[i];
    }
    baseline = baseline/10.0;
    
    double *a_clear = (double *)calloc(128, sizeof(double));
    double *cTr = (double *)calloc(128, sizeof(double));
    double *cs = (double *)calloc(128, sizeof(double));
    
    
    int i_min_s = 0;
    
    for (i = 1; i < 128; i++) {
        if ((a[i]-baseline) > 0) {
            a_clear[i] = 0.0;
        }
        else {
            a_clear[i] = a[i]-baseline;
        }
        
        cs[i] = cTr[i] = a_clear[i]; 
    }
    
    for (i = 1; i < 128; i++) {
        cTr[i] = cTr[i-1] + a_clear[i] - a_clear[i-1]*(1.0-1.0/TAU);
    }
    
    for (i = 1; i < 128; i++) {
        if (i - L - K >= 0) {
            cs[i] = cs[i-1] + cTr[i] - cTr[i-K] - cTr[i-L] + cTr[i-K-L];
        }
        else if (i - L >= 0) {
            cs[i] = cs[i-1] + cTr[i] - cTr[i-K] - cTr[i-L];
        }
        else if (i - K >= 0) {
            cs[i] = cs[i-1] + cTr[i] - cTr[i-K];
        }
        else {
            cs[i] = cs[i-1] + cTr[i];
        }
    }
    
    for (i = 1; i < 128; i++) {
        if (a[i] <= 0.995*a[i-1]) {
            i_min_s = i + K + 5; //+3 or +7
            break;
        }
    }
    
    if (i_min_s+L-K > 128-1) {
        return 0;
    }
    
    double res = 0.0;
    for (i = i_min_s; i < i_min_s+10; i++) { //i<i_min_s + L - K - 1 or 8 - 1
        res += cs[i];
    }
    res = res/(10); //res = res/(L-K-1) or res/8
    
    //res = 0.0;
    //for(i=i_min_s-3; i<i_min_s-3+10; i++) {
    //    res += a_clear[i];
    //}
    
    //printf("i min s = %d, res = %.2f\n", i_min_s, cs[i_min_s]);
    
    free(a_clear);
    free(cTr);
    free(cs);
    
    return fabs(res*EN_NORMAL);
}

/*
double cftrace_t(int *a)
{
    int i;
    double *cCFTrace = (double *)calloc(128, sizeof(double));
    for(i=8; i<128; i++) {
        cCFTrace[i] = 0.5*(9000.0 - a[i-1]) - (9000.0-a[i-8]);
    }
    printf("CFTRACE 1\n");
    
    double baseline = 0.0;
    for(i=8; i<12; i++) {
        baseline += cCFTrace[i];
    }
    baseline = baseline/4.0;
    
    double k = 0.0;
    double b = 0.0;
    double x0 = -1.0;
    
    for(i=12; i<124; i++) {
        if (cCFTrace[i-2] >= baseline && cCFTrace[i-1]-cCFTrace[i] >= 5 && cCFTrace[i-1] >= baseline && cCFTrace[i] <= cCFTrace[i-1] && cCFTrace[i] <= baseline && cCFTrace[i+1] <= baseline-15 && cCFTrace[i+2] <= baseline-40 && cCFTrace[i+3] <= baseline && cCFTrace[i+4] <= baseline) {
            k = (double)(cCFTrace[i] - cCFTrace[i-1]);
            b = (double)(cCFTrace[i] - k*i);
        }
        
        if (k != 0.0) {
            x0 = (baseline-b)/k;
            break;
        }
    }
    printf("CFTRACE 2\n");
    
    free(cCFTrace);

    return x0;
}
*/


double cftrace_t(int *a)
{
    int i;
    int min_a = min_bubble(a, 128);
    int min_a_i = min_bubble_num(a, 128);
    double bg = 0.0;
    for (i = 0; i < 10; i++) {
        bg += (double)a[i];
    }
    bg = bg/10.0;
    
    double edge = (double)(bg - 0.5*(bg - min_a)); //0.5 for gen
    double b = 0.0; 
    double k = 0.0; 
    double x0 = -1.0;
    
    for (i = 10; i < 128; i++) {
        if((a[i] <= edge) && (a[i-1] >= edge)) {
            k = (double)(a[i] - a[i-1]);
            b = (double)(a[i] - k*i);
        
            if (fabs(k) > EPS) {
                x0 = (edge - b)/k;
                break;
            }
        }
    }
    
    return x0;
}


void write_data_to_files(int *histo_en[], int *start[], int set)
{
    int i, j;
    FILE *fout;
    int out;
    struct stat st = {0};
    gchar *tempstr;
    
    tempstr = g_strdup_printf("./histo/online/%d", set);
    if (stat(tempstr, &st) == -1) {
        mkdir(tempstr, 0777);
    }
    
    for (i = 0; i < 4; i++) {
        tempstr = g_strdup_printf("./histo/online/%d/en%d", set, i);
        fout = fopen(tempstr, "w+");
                         
        printf("fopen EN in cycle OK\n");
        for (j = 0; j < HIST_SIZE; j++) {
            fprintf(fout, "%d %d\n", j, histo_en[i][j]);
        }
                         
        fclose(fout);
        g_free(tempstr);
                         
        tempstr = g_strdup_printf("./histo/online/%d/BUFKA%d.SPK", set, i+1);    
        out = open(tempstr, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
        if (out < 0) {
            perror("error in open BUFKA");
            exit(1);
        }
        lseek(out, 512, 0);
        write(out, histo_en[i], HIST_SIZE*sizeof(int));
        close(out);
                         
        g_free(tempstr);
    }
                
    for (i = 0; i < 12; i++) {
        tempstr = g_strdup_printf("./histo/online/%d/t%d", set, i);
        fout = fopen(tempstr, "w+");
                
        printf("fopen T in cycle OK\n");
        for (j = 0; j < HIST_SIZE; j++) {
            fprintf(fout, "%d %d\n", j, start[i][j]);
        }
                     
        fclose(fout);
        g_free(tempstr);
                    
        tempstr = g_strdup_printf("./histo/online/%d/TIME%d.SPK", set, i+1);     
        out = open(tempstr, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
        if (out < 0) {
            perror("error in open TIME");
            exit(1);
        }
        lseek(out, 512, 0);
        write(out, start[i], HIST_SIZE*sizeof(int));
        close(out);
                     
        g_free(tempstr); 
    }
    printf("Save to file end\n");
}


void *read_from_ep(void *user_data)
{
	printf("read from ep\n");
	static char reset_start = 1;
	
	int x, i, j, r, transferred = 0;
    int set = 0;
	unsigned char *buf = (unsigned char *)malloc(4*SIZEOF_DATA(FILETYPE)*sizeof(unsigned char));
	int d[4] = {0, 0, 0, 0};
    
    int *histo_en[4];
    for (i = 0; i < 4; i++) {
        histo_en[i] = (int*)calloc(HIST_SIZE, sizeof(int));
    }
    int *start[12];
    for (i = 0; i < 12; i++) {
        start[i] = (int*)calloc(HIST_SIZE, sizeof(int));
    }
    int s1, s2;
    int n1, n2;
    double start_a, start_b, diff_time, c = 2.0*T_SCALE[0]*T_SCALE[1]/HIST_SIZE;
    int dtime, goodness;
    
    static const int CONTROL_REQUEST_TYPE_IN = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE;
    
    struct timeval tv1, tv2;
    
    //FIFOs pre-settings. Look at the end of this function
    int fp[16][4];
    gchar *tempstr;
    
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 4; j++) {
            tempstr = g_strdup_printf("./fifos/%s_%d_%d", FIFO_FILE_NAME, i, j);
            printf("Tempstr = %s\n", tempstr);
            r = mknod(tempstr, S_IFIFO | 0666, 0);
            //if (r == -1) {
            //    perror("Error in mknod");
            //}
            fp[i][j] = open(tempstr, O_RDWR | O_NONBLOCK);
            if (fp[i][j] == -1) {
                perror("Error in fifo file open");
            //    return NULL;
            }
        }
        g_free(tempstr);
    }
    
	if (!access(files.name_to_save, F_OK)) {
		remove(files.name_to_save);
		printf("File was removed\n");
	}
    
	if (!inFile) inFile = open (files.name_to_save, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	printf("TRUE %d; inFile = %d\n", ok_read, inFile);
	int out;
	while(TRUE) {
		if ((out = control_test(CONTROL_REQUEST_TYPE_IN)) != 1) { times_asked++; continue; }
		if (ok_read == 1) {
            times_started++;
			
			//memset(buf, 0, 4*SIZEOF_DATA(FILETYPE)*sizeof(unsigned char));
            
			r = cyusb_bulk_transfer(h, IN_EP, buf, 4*SIZEOF_DATA(FILETYPE), &transferred, 0);
			if (r != 0) {
				if(transferred != 4*SIZEOF_DATA(FILETYPE))
					printf("Error in bulk transfer %d, transferred = %d \n", r, transferred);
				cyusb_error(r);
				cyusb_close();
				exit(0);
			}
            
			for (j = 0; j < 4; j++) {
                d[j] = (buf[512*(j+1)-1] >> 6)+1;
            
				for (i = 0; i <= SIZEOF_DATA(FILETYPE)/2-1; i++) {
					data[j][i] = buf[2*i+512*j]+256*(buf[2*i+1+512*j]&0b00111111);
				}
				for (i = 0; i < SIZEOF_DATA(FILETYPE)/4; i++) {
					swap(&(data[j][i]), &(data[j][SIZEOF_DATA(FILETYPE)/2-i-1]));
				}
                data[j][SIZEOF_DATA(FILETYPE)/2-1] = d[j];
            
            // WRITE DATA TO FILE ON/OFF    
			/*	if( write(inFile, data[j], sizeof(int)*SIZEOF_DATA(FILETYPE)/2) == -1 ) {
					perror("Error in read data!");
					printf("fd = %d\n", inFile);
					exit(1);
				}
            */
                
			}
            
            goodness = 0;
            for (j = 0; j < 2; j++) {
                //if ( (check_overflow_in_signal(data[2*j]) == -1) || (check_overflow_in_signal(data[2*j+1]) == -1) ) {
                //    continue;
                //}
            
                n1 = d[2*j];
                n2 = d[2*j+1];
                
                s1 = (int)area(data[2*j]);
                s2 = (int)area(data[2*j+1]);
                
                if (s1 >= EN_THRESHOLD && s1 < HIST_SIZE) {
                    histo_en[n1-1][s1]++;
                }
                if (s2 >= EN_THRESHOLD && s2 < HIST_SIZE) {
                    histo_en[n2-1][s2]++;
                }
                //printf("s1 = %d, s2 = %d, n1 = %d, n2 = %d\n", s1, s2, n1, n2); //CHANGE IT!!!
                
                if ( s1 >= EN_THRESHOLD && s1 < HIST_SIZE && s2 >= EN_THRESHOLD && s2 < HIST_SIZE ) {
                    start_a = cftrace_t(data[2*j]);
                    start_b = cftrace_t(data[2*j+1]);
                    
                    diff_time = T_SCALE[0]*(start_a-start_b) + T_SCALE[0]*T_SCALE[1];
                    dtime = 0;
                    for (i = 0; i < HIST_SIZE-1; i++) {
                        if (( definitely_greater_than(i*c, diff_time) ) && ( definitely_greater_than(diff_time, (i+1)*c) )) {
                            dtime = i;
                            break;
                        }
                    }
                    
                    //printf("dtime = %d s_a = %.2f s_b = %.2f\n", dtime, start_a, start_b);

                    if ((n1 == 1) && (n2 == 2)) {
                        if ((s1 >= EN_RANGE[0][0]) && (s1 <= EN_RANGE[0][1]) && (s2 >= EN_RANGE[1][2]) && (s2 <= EN_RANGE[1][3])) {
                            start[0][dtime]++; //T12
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[0][2]) && (s1 <= EN_RANGE[0][3]) && (s2 >= EN_RANGE[1][0]) && (s2 <= EN_RANGE[1][1])) {
                            start[1][dtime]++; // T21
                            goodness++;
                        }
                    }
                    else if ((n1 == 1) && (n2 == 3)) {
                        if ((s1 >= EN_RANGE[0][0]) && (s1 <= EN_RANGE[0][1]) && (s2 >= EN_RANGE[2][2]) && (s2 <= EN_RANGE[2][3])) {
                            start[2][dtime]++; //T13
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[0][2]) && (s1 <= EN_RANGE[0][3]) && (s2 >= EN_RANGE[2][0]) && (s2 <= EN_RANGE[2][1])) {
                            start[3][dtime]++; //T31
                            goodness++;
                            }
                    }
                    else if ((n1 == 1) && (n2 == 4)) {
                        if ((s1 >= EN_RANGE[0][0]) && (s1 <= EN_RANGE[0][1]) && (s2 >= EN_RANGE[3][2]) && (s2 <= EN_RANGE[3][3])) {
                            start[4][dtime]++; //T14
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[0][2]) && (s1 <= EN_RANGE[0][3]) && (s2 >= EN_RANGE[3][0]) && (s2 <= EN_RANGE[3][1])) {
                            start[5][dtime]++; //T41
                            goodness++;
                        }
                    }
                    else if ((n1 == 2) && (n2 == 3)) {
                        if ((s1 >= EN_RANGE[1][0]) && (s1 <= EN_RANGE[1][1]) && (s2 >= EN_RANGE[2][2]) && (s2 <= EN_RANGE[2][3])) {
                            start[6][dtime]++; //T23
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[1][2]) && (s1 <= EN_RANGE[1][3]) && (s2 >= EN_RANGE[2][0]) && (s2 <= EN_RANGE[2][1])) {
                            start[7][dtime]++; //T32
                            goodness++;
                        }
                    }
                    else if ((n1 == 2) && (n2 == 4)) {
                        if ((s1 >= EN_RANGE[1][0]) && (s1 <= EN_RANGE[1][1]) && (s2 >= EN_RANGE[3][2]) && (s2 <= EN_RANGE[3][3])) {
                            start[8][dtime]++; //T24
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[1][2]) && (s1 <= EN_RANGE[1][3]) && (s2 >= EN_RANGE[3][0]) && (s2 <= EN_RANGE[3][1])) {
                            start[9][dtime]++; //T42
                            goodness++;
                        }
                    }
                    else if ((n1 == 3) && (n2 == 4)) {
                        if ((s1 >= EN_RANGE[2][0]) && (s1 <= EN_RANGE[2][1]) && (s2 >= EN_RANGE[3][2]) && (s2 <= EN_RANGE[3][3])) {
                            start[10][dtime]++; //T34
                            goodness++;
                        }
                        else if ((s1 >= EN_RANGE[2][2]) && (s1 <= EN_RANGE[2][3]) && (s2 >= EN_RANGE[3][0]) && (s2 <= EN_RANGE[3][1])) {
                            start[11][dtime]++; //T43
                            goodness++;
                        }
                    }
                }
            }

            if (goodness >= goodness_lim) {
                //printf("goodness = %d, s1 = %d, s2 = %d times = %d\n", goodness, s1, s2, 4*times_started-4);
                for (j = 0; j < 4; j++) {
                    if ( write(inFile, data[j], sizeof(int)*SIZEOF_DATA(FILETYPE)/2) == -1 ) {
                        perror("Error in write data!");
                        printf("fd = %d\n", inFile);
                        exit(1);
                    }
                }
            }

            //Write to FIFO fps
            for (i = 0; i < 16; i++) {
                //printf("written\n");
                for (j = 0; j < 4; j++) {
                    if (i < 4) {
                        write(fp[i][j], &(histo_en[i][j*HIST_SIZE/4]), HIST_SIZE/4*sizeof(int));
                    }
                    else {
                        write(fp[i][j], &(start[i-4][j*HIST_SIZE/4]), HIST_SIZE/4*sizeof(int));
                    }
                }
            }
            
            //Write to SPK files
            if (times_started % TIMES_TO_WRITE_SPK == 0) {
                //printf("Save to file start\n");
                write_data_to_files(histo_en, start, set);
                
                set++;
            }
        }
        else if (ok_read == 2) {
            write_data_to_files(histo_en, start, set);
            
            break;
        }
    }
	
    printf("free\n");
	free(buf);
    printf("free buf\n");
    for (i = 0; i < 4; i++) {
        printf("free histo en i = %d\n", i);
        if (histo_en[i] != NULL) {
            free(histo_en[i]);
            histo_en[i] = NULL;
        }
    }
    for (i = 0; i < 12; i++) {
        if (start[i] != NULL) {
            free(start[i]);
            start[i] = NULL;
            printf("free start i = %d\n", i);
        }
    }
    printf("free 32\n");
	close(inFile);
    
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 4; j++) {
            close(fp[i][j]);
        }
    }
    printf("free end\n");
    return NULL;
}


static void button_read_cb(GtkWidget *widget, gpointer   user_data)
{
    int err = -1; 
    
    err = pthread_create(&tid1, NULL, read_from_ep, NULL);
    if (err != 0) {
	fprintf(stderr, "\ncan't create clock thread :[%s]", strerror(err));
	return ;
    }

    err = pthread_create(&tid2, NULL, draw, NULL);
    if (err != 0) {
	fprintf(stderr, "\ncan't create clock thread :[%s]", strerror(err));
	return ;
    }
}


static void button_write_cb(GtkWidget *widget, gpointer   data)
{
	int err = pthread_create(&tid1, NULL, write_to_ep, NULL);
    if (err != 0)
		printf("\ncan't create clock thread :[%s]", strerror(err));
	
	err = pthread_create(&tid2, NULL, draw, NULL);
    if (err != 0)
		printf("\ncan't create clock thread :[%s]", strerror(err));
}


int send_command(const char *command, int args) 
{
	int r, i, transferred = 0;
	unsigned char *buf = (unsigned char *)calloc(256*2, sizeof(unsigned char));
	
	//word #1 = 1; word #2 = porog - установка порога
	//word #1 = 3; word #2 = 1 - сброс (reset)
	//word #1 = 2; word #2 = задержка - установка задержки
	//word #1 = 5; word #2 = 5 - test
    
    //new
    //word #1 = 1; word #2 = porog - установка порога D1
    //word #1 = 2; -||- D2
    //word #1 = 3; -||- D3
    //word #1 = 4; -||- D4
    //word #1 = 5; word #2 = 1/2 ON/OFF coinc
    //word #1 = 6; word #2 = window - Coinc's window
    //word #1 = 7; word #2 = 1 - сброс (reset)
	
	if (!strcmp(command, "s0") || !strcmp(command, "s1") || !strcmp(command, "s2") || !strcmp(command, "s3")) {
        buf[0] = (unsigned char)(command[1]-'0') + 1;
        buf[2] = args - 256*(args/256);
        buf[3] = args/256;
        //buf[2] = 0b11101000;
        //buf[3] = 0b00000011;
        //buf[2] = buf[3] = 0;
        printf("command = %s rangeset buf[0] = %u buf[2] = %u buf[3] = %u args = %d\n", command, buf[0], buf[2], buf[3], args);
        
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in set range!");
            printf("transferred %d bytes\n", transferred);
        }
        free(buf);
    }
    else if (!strcmp(command, "r")) {
        buf[0] = 7;
        buf[2] = 1;
        
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in reset!");
        }
        free(buf);
        
        printf("reset\n");
	}
    else if (!strcmp(command, "w")) {
        buf[0] = 6;
        buf[2] = args - 256*(args/256);
        buf[3] = args/256;
			
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in set delay!");
        }
        free(buf);
        
        printf("wait buf[2] = %u buf[3] = %u\n", buf[2], buf[3]);
    }
    else if (!strcmp(command, "t")) {
        buf[0] = 1;
        buf[2] = 5;
        
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in test!");
        }
        free(buf);
			
        printf("test");
    }
    else if (!strcmp(command, "c")) {
        buf[0] = 5;
        buf[2] = 1;
        
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in test!");
        }
        free(buf);
			
        printf("coinc on\n");
    }
    else if (!strcmp(command, "n")) {
        buf[0] = 5;
        buf[2] = 2;
			
        r = cyusb_bulk_transfer(h, OUT_EP, buf, 256*2, &transferred, TIMEOUT);
        if (r != 0) {
            cyusb_error(r);
            cyusb_close();
            perror("Error in test!");
        }
        free(buf);
			
        printf("coinc off\n");
	}
}


static void button_range_cb(GtkWidget *button, gpointer   data)
{
	int i;
	gchar *tempstr;
	
	for (i = 0; i < 4; i++) {
		if (button == button_set_range[i]) {
			rangeset[i] = atoi(gtk_entry_get_text(GTK_ENTRY(entry_range[i])));
			tempstr = g_strdup_printf("s%d", i);
			send_command(tempstr, rangeset[i]);
			g_free(tempstr);	
		}
	}
}

static void button_reset_cb(GtkWidget *widget, gpointer   data)
{
	send_command("r", 0);
}

static void button_delay_cb(GtkWidget *widget, gpointer   data)
{
	delay = atoi(gtk_entry_get_text(GTK_ENTRY(entry_delay)));
	
	send_command("w", delay);
    send_command("r", 0);
}

static void button_test_cb(GtkWidget *widget, gpointer   data)
{
	send_command("t", 0);
}

static void button_coinc_on_cb(GtkWidget *widget, gpointer   data)
{
	//ON coincidence mode
	printf("coincidence\n");
	
	send_command("c", delay);
}

static void button_coinc_off_cb(GtkWidget *widget, gpointer   data)
{
	//OFF coincidence mode
	send_command("n", delay);
}

void *draw(void *user_data) 
{
	static int sixty_seconds = 0;
	static int prev_times_started = 0;
	const int sleep_time = 5;
	const int acquisition_time = acq_time;
	gchar *tempstr;
	
	while (TRUE) {
		printf("DRAW! sixty_seconds = %d\n", sixty_seconds);
		sleep(sleep_time);
		pthread_mutex_lock( &mutex1 );
		
		printf("Queue draw start\n");
		gtk_widget_queue_draw_area(main_window, 0, 0, 500, 150);
		gtk_widget_queue_draw_area(main_window, 0, 150, 500, 300);
		gtk_widget_queue_draw_area(main_window, 0, 300, 500, 450);
		gtk_widget_queue_draw_area(main_window, 0, 450, 500, 600);
		printf("Queue draw end\n");
        
		sixty_seconds += sleep_time;
		printf("Times = %d; Times asked = %ld\n", times_started, times_asked);
		intens = (times_started-prev_times_started)/sleep_time;
		prev_times_started = times_started;
		
        printf("SET text in entry start\n");
        tempstr = g_strdup_printf("%d cts/s | %d s", 2*intens, sixty_seconds);
		gtk_label_set_text(GTK_LABEL(label_intens), tempstr);
		g_free(tempstr);
        printf("SET text in entry end\n");
        
		if (sixty_seconds == acquisition_time) { printf("Exit times_started = %d\n", times_started); ok_read = 2; pthread_join(tid1, NULL); pthread_exit(&tid2);}
		
		pthread_mutex_unlock( &mutex1 );
	}
}

int draw_data_from_file(GtkTextBuffer *txtbuffer)
{
    gchar *tempstr;
    
    tempstr = g_strdup_printf("File name to read: %s #%d", files.name_to_read, files.readpos);
	gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), files.sb_context_id, tempstr);
    g_free(tempstr);
	
	if (FILETYPE) {
		if ( lseek(files.fd_read, SIZEOF_DATA(FILETYPE)*sizeof(int)*files.readpos, SEEK_SET) < 0 ) 
			return -2;
			
		if ( read(files.fd_read, data[0], SIZEOF_DATA(FILETYPE)/2*sizeof(int)) == -1 )
			return -3;
			
		if ( read(files.fd_read, data[1], SIZEOF_DATA(FILETYPE)/2*sizeof(int)) == -1 )
			return -3;
	}
	else {
		int i, j;
		
		if (fseek(files.fd_Fread, ftell(files.fd_Fread), SEEK_SET) != 0)
			return -2;
		
		for (i = 0; i < SIZEOF_DATA(FILETYPE)/2; i++) {
			fscanf(files.fd_Fread, "%d %d\n", &j, &data[0][i]);
		}
		for (i = 0; i < SIZEOF_DATA(FILETYPE)/2; i++) {
			fscanf(files.fd_Fread, "%d %d\n", &j, &data[1][i]);
		}
		
		int max0 = max_bubble(data[0], SIZEOF_DATA(FILETYPE))
			, max1 = max_bubble(data[1], SIZEOF_DATA(FILETYPE));
		for (i = 0; i < SIZEOF_DATA(FILETYPE)/2; i++) {
			data[0][i] = 4000*data[0][i]/max0;
			data[1][i] = 4000*data[1][i]/max0;
		}
	}
		
	gtk_widget_queue_draw(main_window);
	
	calc_results(txtbuffer, data[0], data[1]);
	
	return 0;
}

static void button_readfile_cb(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *dialog;
	GtkFileChooser *chooser;
	GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
	gint i, res;

	dialog = gtk_file_chooser_dialog_new ("Read File",
                                      GTK_WINDOW(main_window),
                                      action,
										"_Cancel",
                                      GTK_RESPONSE_CANCEL,
                                      "_Open",
                                      GTK_RESPONSE_ACCEPT,
                                      NULL);
	chooser = GTK_FILE_CHOOSER (dialog);

	gtk_file_chooser_set_do_overwrite_confirmation (chooser, TRUE);


	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res == GTK_RESPONSE_ACCEPT)
	{
	    if (files.name_to_read != NULL)
		free(files.name_to_read);
	    
	    files.name_to_read = (char *)malloc(strlen(gtk_file_chooser_get_filename (chooser))*sizeof(char));
		
	    files.name_to_read = gtk_file_chooser_get_filename (chooser);
	    printf("filename choosen = %s %lu\n", files.name_to_read, strlen(gtk_file_chooser_get_filename (chooser)));
		
	    if ( strncmp(files.name_to_read, "6_1__", strlen("6_1__")) == 0 ) {
		FILETYPE = 0;
	    }
	    else {
		FILETYPE = 1;
	    }
	    
	    if (FILETYPE) {
		files.fd_read = open(files.name_to_read, O_RDONLY);
	    }
	    else {
		files.fd_Fread = fopen(files.name_to_read, "r");
	    }
	    
	    files.readpos = 0;
	    printf("FT = %d\n", FILETYPE);
	    if ( draw_data_from_file((GtkTextBuffer *)user_data) != 0 ) {
		if (FILETYPE) {
		    close(files.fd_read);
		}
		else {
		    fclose(files.fd_Fread);
		}
	    }
	}

	gtk_widget_destroy (dialog);
}

static void button_readfile_next_cb(GtkWidget *widget, gpointer   user_data)
{
    files.readpos += 2;

    if ( draw_data_from_file((GtkTextBuffer *)user_data) != 0 ) {
	close(files.fd_read);
    }
}

static void button_readfile_prev_cb(GtkWidget *widget, gpointer   user_data)
{
    files.readpos-=2;
    if (files.readpos < 0) {
	files.readpos = 0;
    }

    if ( draw_data_from_file((GtkTextBuffer *)user_data) != 0 ) {
	close(files.fd_read);
    }
}

int find_pick_start_stop(int *a, int *max_num, int *min_num)
{
    int i, j;
    int baseline = (a[0] + a[1] + a[2] + a[3] + a[4]) / 5;
     
    for (i = 2, j = 1; i <= SIZEOF_DATA(FILETYPE)-1; i++) {
        if (a[i] <= 0.95*a[j]) {
	    //	printf("i = %d, a[i] = %d, a[j] = %d\n", i, a[i], a[j]);
	    if( (a[i+1] <= a[j]) && (a[i+2] <= a[j]) && (a[i] <= baseline) ) {
                j = i;
	    }
	    break;
        }
    }
    if (j < 7) {
	j = 7;
    }
    *max_num = j-7;
    
    for (i = j; i <= SIZEOF_DATA(FILETYPE)-4; i++) {
	if ((a[i] >= a[j-7]) &&	(a[i+1]>=a[j-7]) && (a[i]>=baseline)) {
	    *min_num = i; 
	    return 0;
	}
    }
    
    //*min_num = 150;
    
    return -1;
}

double trap_area(int *a, int max_num, int min_num, double *base)
{
    int i;
    double area = 0.0;
    double baseline = (a[0] + a[1] + a[2] + a[3] + a[4]) / 5.0;
    if (base != NULL) {
	*base = baseline;
    }
    
    for (i = max_num; i <= min_num-1; i++) {
	area += 0.5*(a[i]+a[i+1]-2.0*baseline);
    }

    return area;
}

double trapez_shape_area(int *a, int max_num, int min_num)
{	
    const int k = 10;
    const int l = 15;
    const double tau = 2.4;

    double *s = (double *)malloc(sizeof(double)*min_num); //output signal
    int *Tr = (int *)calloc(min_num, sizeof(int));		   //input signal after zero-pole correction and -baseline
    double baseline = (a[0] + a[1] + a[2] + a[3] + a[4] + a[5] + a[6] + a[7] + a[8] + a[9])/10.0, min_s;
    int i, i_min_s;
    printf("Baseline = %.4f\n", baseline);
    
    min_s = s[0] = 0.0;
    i_min_s = max_num;
	
    for (i = 0; i <= SIZEOF_DATA(FILETYPE)-1; i++) {
	//	a[i] = a[i]-baseline;
	//	a[i] =  a[i]-2000;
	//	a[i] = 2000 - a[i];
    }
    for (i = 1; i <= min_num-1; i++) {
	Tr[i] = Tr[i-1] + ( (a[i]-baseline) - (a[i-1]-baseline)*(1.0-1.0/tau) );
	//	printf("Tri = %d\t", Tr[i]);
    }
	
    for (i = max_num+1; i <= min_num-1; i++) {
	if (i-l-k>=0)
	    //s[i] = s[i-1] + a[i] - a[i-k] - a[i-l] + a[i-k-l];
	    s[i] = s[i-1] + Tr[i] - Tr[i-k] - Tr[i-l] + Tr[i-k-l];
	else if (i-l>=0)
	    //s[i] = s[i-1] + a[i] - a[i-k] - a[i-l];
	    s[i] = s[i-1] + Tr[i] - Tr[i-k] - Tr[i-l];
	else if (i-k>=0)
	    //s[i] = s[i-1] + a[i] - a[i-k];
	    s[i] = s[i-1] + Tr[i] - Tr[i-k];
	else
	    //s[i] = s[i-1] + a[i];
	    s[i] = s[i-1] + Tr[i];
	//	if(s[i] < min_s) { min_s = s[i]; i_min_s = i; }
	//	printf("si=%.2f, si-1=%.2f, ai=%d, ai-k=%d, ai-k-l=%d\n", s[i], s[i-1], a[i], a[i-k], a[i-k-l]);
    }
	
    FILE *in = fopen("trapez_shape", "w+");
    for (i = max_num+1; i <= min_num-1; i++) {
	fprintf(in, "%d %d %e\n", i, a[i], s[i]);
	//	a[i] = (int)s[i];
    }
    fclose(in);
    
    for (i = 1; i <= min_num-1; i++) {
	if (a[i] <= 0.95*a[i-1]) {
	    i_min_s = i + k + 1;
	    break;
	}
    }
    printf("i_min_s = %d\n", i_min_s);
    
    double res = 0.0;
    for (i = i_min_s; i <= i_min_s + l-k - 2; i++)
		res += s[i];
    res = res/(l-k-1);
    
    free(s);
    free(Tr);
	
    return res;
}

// f(x) = ( A*(exp(-((x)-(x0))/a) - exp(-((x)-(x0))/b)) -B*exp(-((x)-(x0))/c) + F )
#define f(x, x0) ( -2.49991703469447e-09*(exp(-((x)-(x0))/115069.384046802) - exp(-((x)-(x0))/1.45088424300257)) - 517.224857987907*exp(-((x)-(x0))/24.9381408089486) + 2506.78894844553 )

//PICK to approx may10_gen_rt=25_dt=500_1sqrt2 #0 (without attenuation) square = -27702.6
#define NORMAL_SQUARE	(-27702.6)

double search_min_f(double x0, double dx, double start_search)
{
    int i;
    double z = f(start_search, x0), x0min;
	
    printf("x0=%e, dx=%e, start=%e\n", x0, dx, start_search);
	
    for (i = 1; i < (int)(6.0/dx); i++) {
	if (f(start_search+i*dx, x0) < z) {
	    z = f(start_search+i*dx, x0);
	    x0min = start_search + i*dx;
	}
    }
	
    return x0min;
}

double find_start_pick(int *x, char flag)
{
    //	#define f(x, x0) ( -0.000725124002756408*(exp(-((x)-(x0))/27461468.6) - exp(-((x)-(x0))/1.31784519694857)) -1855.99996745509*exp(-((x)-(x0))/34.3120374841288) + 3298.00001427814 )
#define STEP	0.001 
#define	EPSILON	0.000001
    double x0INIT = 52.0;
	
    double S_optim, S_plus, S_minus, S_test;
    double S_plus_prev, S_minus_prev;
    double x0new = x0INIT;
    int i, j;
    int start_approx = 13;
    int end_approx = 20;
	
    int x_min_num = min_bubble_num(x, SIZEOF_DATA(FILETYPE));
    
    if(flag == 0) {
	double baseline = (x[0] + x[1] + x[2] + x[3] + x[4]) / 5.0;
	int max, min;
	find_pick_start_stop(x, &max, &min);
	
	double normalization = NORMAL_SQUARE/trap_area(x, max, min, NULL);
	printf("normalization = %.4f\n", normalization);
	
	for(i=0; i<=SIZEOF_DATA(FILETYPE)-1; i++) {
	    x[i] = (int)( baseline - normalization*(baseline-(double)x[i]) );
	} 
    }
	
    for(i=0; i<x_min_num; i++) {
	if((x[i] <= 0.97*x[i-1]) && (x[i] <= 0.97*x[i-1])) {
	    start_approx = i;
	    break;
	}
    }
 
    end_approx = start_approx + 7;
    printf("start = %d; end = %d approx\n", start_approx, end_approx);
    /*
      for(i = x_min_num; i<SIZEOF_DATA(FILETYPE)-1; i++) {
      if( (x[i] >= 0.7*x[x_min_num]) && (x[i+1] >= 0.7*x[x_min_num-4]) ) {
      x0INIT = (double)(i);
      break;
      }
      }
	printf("x_min_num = %d, x0INIT = %.4f\n", x_min_num, x0INIT);
    */
    S_optim = 0.0;
    S_test = 0.0;
    
    for(i=start_approx; i<=end_approx-1; i++) {
	S_optim += fabs(x[i] - f((double)i, x0INIT));
	S_test += fabs(x[i] - f((double)i, 53.7495));
    }
    
    S_plus = S_minus = 0.0;
    for(j=1; j <= (int)(3.0/STEP); j++) {
	S_plus_prev = S_plus;
	S_minus_prev = S_minus;
	S_plus = S_minus = 0.0;
	
	for(i=start_approx; i<=end_approx-1; i++) {
			S_plus += fabs(x[i] - f((double)i, x0INIT + j*STEP));
			S_minus += fabs(x[i] - f((double)i, x0INIT - j*STEP));
	}
	
	if(S_plus <= S_minus) {
	    if(S_plus < S_optim) {
		S_optim = S_plus;
		x0new = x0INIT + STEP*j;
				if(fabs(S_plus - S_plus_prev) <= EPSILON) {
				    break;
				}
	    }
		}
	else {
	    if(S_minus < S_optim) {
		//		printf("j=%d Soptim = %.2f, S+ = %.2f, S- = %.2f, S_test = %.2f; x0new = %.2f\n", j, S_optim, S_plus, S_minus, S_test, x0new);
		S_optim = S_minus;
		x0new = x0INIT - STEP*j;
		if(fabs(S_minus - S_minus_prev) <= EPSILON) {
		    break;
		}
	    }
	}
    }
	
    printf("j=%d Soptim = %.2f, S+ = %.2f, S- = %.2f, S_test = %.2f; x0new = %.2f\n", j, S_optim, S_plus, S_minus, S_test, x0new);
    
    return x0new;
//	return search_min_f(x0new, STEP/10.0, start_approx);
}


double find_start_cftrace(int *x)
{
	int i;
	
	int *CFTrace = (int *)calloc(SIZEOF_DATA(FILETYPE), sizeof(int));
	for(i=3; i<=SIZEOF_DATA(FILETYPE)-1; i++) {
		CFTrace[i] = 0.5*(3000-x[i-1]) - (3000-x[i-1-2]);
	}
	FILE *CFTfile = fopen("cftrace.txt", "w");
	for(i=1; i<=SIZEOF_DATA(FILETYPE)-1; i++) {
		fprintf(CFTfile, "%d %d\n", i, CFTrace[i]);
	}
	fclose(CFTfile);
	
	double background = (CFTrace[5] + CFTrace[6] + CFTrace[7] + CFTrace[8] + CFTrace[9])/5.0;
	double a, b, x0 = -1.0;
	
	for(i=12; i<SIZEOF_DATA(FILETYPE)-1; i++) {
		if((CFTrace[i-1] >= background) && (CFTrace[i] <= CFTrace[i-1]) && (CFTrace[i] <= background) && (CFTrace[i+1] <= background) && (CFTrace[i+2] <= background)) {
			printf("i = %d, background = %.2f, data[5] = %d\n", i, background, CFTrace[5]);
			a = (double)(CFTrace[i]-CFTrace[i-1]);
			b = (double)CFTrace[i] - a*i;
			printf("a = %.2f b = %.2f\n", a, b);
			
			x0 = (background - b)/a;
			printf("x0 = %.2f\n", x0);
			break;
		}
	}
	
	free(CFTrace); 

	return x0;
}


void calc_results(GtkTextBuffer *txtbuffer, int *a, int *b)
{
	double s_trap[2];
	int max[2], min[2];
	double base[2];
	GtkTextIter iter_start, iter_stop;
	gchar *tempstr;
	
	gtk_text_buffer_get_iter_at_line(txtbuffer, &iter_start, BUFFER_EN_PLACE);
	gtk_text_buffer_get_iter_at_line_offset(txtbuffer, &iter_stop, BUFFER_EN_PLACE+4, 0);
	gtk_text_buffer_delete(txtbuffer, &iter_start, &iter_stop);
	
	find_pick_start_stop(a, &max[0], &min[0]);
//	max[0] -= 5;
//	min[0] = 80;
//	s_trap[0] = trap_area(a, max[0], min[0], &base[0]);
	//s_trap[0] = trapez_shape_area(a, 0, 100);
	s_trap[0] = area(a);
    printf("max:%d, min:%d; a[16]=%d\n", max[0], min[0], a[16]);
	
	find_pick_start_stop(b, &max[1], &min[1]);
//	max[1] -= 5;
//	min[1] = 80;
//	s_trap[1] = trap_area(b, max[1], min[1], &base[1]);
	s_trap[1] = area(b);
	printf("strap = %.4f, %.4f\n", s_trap[0], s_trap[1]);
	
	//start[0] = find_start_pick(a, 0);
	//start[1] = find_start_pick(b, 1);
	start[0] = cftrace_t(a);
	start[1] = cftrace_t(b);
	printf("start = %.2f, %.2f\n", start[0], start[1]);
	
	//find_start_pick_lsm(a);
	
	tempstr = g_strdup_printf("E = %.3fK - %.1f (%d-%d), %.3fK - %.1f (%d-%d)\n", s_trap[0]/1000.0, base[0], max[0], min[0], s_trap[1]/1000.0, base[1], max[1], min[1]);
	gtk_text_buffer_get_iter_at_line(txtbuffer, &iter_start, BUFFER_EN_PLACE);
	gtk_text_buffer_insert_with_tags_by_name(txtbuffer, &iter_start, tempstr, -1, "blue_foreground", NULL);
	g_free(tempstr);
	
	tempstr = g_strdup_printf("T = %.4f, %.4f\n", start[0],  start[1]);							
	gtk_text_buffer_get_iter_at_line(txtbuffer, &iter_start, BUFFER_T_PLACE);
	gtk_text_buffer_insert_with_tags_by_name(txtbuffer, &iter_start, tempstr, -1, "red_foreground", NULL);
	g_free(tempstr);
					
	tempstr = g_strdup_printf("ᐃT = %.4f\n", start[0]-start[1]);		
	gtk_text_buffer_get_iter_at_line(txtbuffer, &iter_start, BUFFER_dT_PLACE);
	gtk_text_buffer_insert_with_tags_by_name(txtbuffer, &iter_start, tempstr, -1, "green_foreground", NULL);
	g_free(tempstr);
}


void print_bin(int x)
{
    int i;
    char s[8];

    for(i=0; i<8; i++) {
        if (x & (1 << i)) {
            s[7-i] = '1';
        }
        else
            s[7-i] = '0';
    }

    printf("0b");
    for(i=0; i<8; i++)
        printf("%c", s[i]);
    printf("\n");
}

static void button_read_once_cb(GtkWidget *widget, gpointer   user_data)
{
	int x, i, j, r, transferred = 0;
	unsigned char *buf = (unsigned char *)malloc(4*SIZEOF_DATA(FILETYPE)*sizeof(unsigned char));

	static const int CONTROL_REQUEST_TYPE_IN = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE;
	static const int CONTROL_REQUEST_TYPE_OUT = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_ENDPOINT;
	//control_test(CONTROL_REQUEST_TYPE_OUT);
	control_test(CONTROL_REQUEST_TYPE_IN);
	
/*	r = cyusb_bulk_transfer(h, IN_EP, buf, 4*SIZEOF_DATA(FILETYPE), &transferred, TIMEOUT);
	if(r != 0) {
		cyusb_error(r);
		cyusb_close();
		exit(0);
	}
			
	for(i=0; i<=SIZEOF_DATA(FILETYPE)-1; i++) {
		data[0][i] = buf[2*i]+256*(buf[2*i+1]&0b00001111);
	}
	for(i=0; i<SIZEOF_DATA(FILETYPE)/2; i++) {
		swap((data[0]+i), (data[0]+SIZEOF_DATA(FILETYPE)-i-1));
	}*/
	
	r = cyusb_bulk_transfer(h, IN_EP, buf, 4*SIZEOF_DATA(FILETYPE), &transferred, 1);
	printf("trans = %d\n", transferred);
	if(r != 0) {
		if(transferred != 4*SIZEOF_DATA(FILETYPE))
			printf("Error in bulk transfer, transferred = %d \n", transferred);
			cyusb_error(r);
			cyusb_close();
			exit(0);
		}
			
	for(j=0; j<4; j++) {
                print_bin(buf[511]);
                print_bin(buf[1023]);
                printf("\n");
                printf("det1 = %d, det2 = %d\n", (buf[511] >> 6)+1, (buf[1023] >> 6)+1);
    
				for(i=0; i<=SIZEOF_DATA(FILETYPE)/2-1; i++) {
					data[j][i] = buf[2*i+512*j]+256*(buf[2*i+1+512*j]&0b00111111);
				}
				for(i=0; i<SIZEOF_DATA(FILETYPE)/4; i++) {
					swap(&(data[j][i]), &(data[j][SIZEOF_DATA(FILETYPE)/2-i-1]));
				}
                
              /*  printf("data[255] = ");
                print_bin(data[j][255]);
                printf("data[254] = ");
                print_bin(data[j][254]);
                printf("\n"); */
                
                printf("a1 = %d a2 = %d a3 = %d a4 = %d\n", (int)area(data[0]), (int)area(data[1]), (int)area(data[2]), (int)area(data[3]));
                printf("td1 = %d td2 = %d\n", (int)(T_SCALE[0]*(cftrace_t(data[0])+T_SCALE[1])), (int)(T_SCALE[0]*(cftrace_t(data[1])+T_SCALE[1])) );
	}
		
	gtk_widget_queue_draw(main_window);
		
	send_command("r", 0);
}

static void scale_zoom_changed(GtkAdjustment *adjust, gpointer       user_data)
{
	//printf("value changed %.2f\n", gtk_adjustment_get_value(adjust));

	scale_opt.x_range = (int) gtk_adjustment_get_value(adjust);
	
	gtk_widget_queue_draw(main_window);
}

static void left_zoom_cb(GtkWidget *widget, gpointer   user_data)
{
	scale_opt.x_prev = scale_opt.x;
	scale_opt.x -= 10;
	
	gtk_widget_queue_draw(main_window);
}

static void right_zoom_cb(GtkWidget *widget, gpointer   user_data)
{
	scale_opt.x_prev = scale_opt.x;
	scale_opt.x += 10;
	
	gtk_widget_queue_draw(main_window);
}

static void yzoom_cb(GtkWidget *widget, gpointer user_data)
{
	scale_opt.y_range -= 50.0;
    scale_opt.y += 25;
	
	gtk_widget_queue_draw(main_window);
}

static void unzoom_cb(GtkWidget *widget, gpointer   user_data)
{
	scale_opt.x_range = SIZEOF_DATA(FILETYPE)/2;
	scale_opt.x = 0;
	scale_opt.y = 0.0;
    scale_opt.y_range = 8200-7100;
    scale_opt.y_up = 0;
	
	gtk_adjustment_set_value(GTK_ADJUSTMENT(user_data), scale_opt.x_range);
	
	gtk_widget_queue_draw(main_window);
}

static void yup_cb(GtkWidget *widget, gpointer   user_data)
{
    scale_opt.y_up += 10;

	gtk_widget_queue_draw(main_window);
}

static void ydown_cb(GtkWidget *widget, gpointer   user_data)
{
    scale_opt.y_up -= 10;

	gtk_widget_queue_draw(main_window);
}


int check_positive_int(char *str)
{
    int i = 0;
    unsigned int len = strlen(str);

    if (str[0] == '-') {
	return -1;
    }

    for (i = 0; i <= len - 1; i++) {
	if (!isdigit(str[i])) {
	    return -1;
	}
    }

    return atoi(str);   
}

int main(int argc, char *argv[])
{
    printf("%d, FT = %d\n", SIZEOF_DATA(FILETYPE), FILETYPE);
    
    if (argc < 3) {
	fprintf(stderr, "The program should be started like followed: %s -o NAME_TO_SAVE\n", argv[0]);
	exit(-1);
    }

    int i = 0, j = 0;
    int ret = -1;
    int x = -1;
    int opt;
	
    for (i = 0; i < 4; i++) {
	data[i] = (int *)malloc(SIZEOF_DATA(FILETYPE)/2*sizeof(int *));
	if (data[i] == NULL) {
	    FREE_DATA(i);

	    fprintf(stderr, "Error in calloc data\n");
	    exit(-1);
	}
    }
	
    scale_opt.x_range = SIZEOF_DATA(FILETYPE)/2;
    scale_opt.x = 0;
    scale_opt.y = 0;
    scale_opt.y_range = 8200-7100;
    scale_opt.y_up = 0;
	
    files.name_to_save = (char *)malloc(128*sizeof(char));
    if(files.name_to_save == NULL) {
	FREE_DATA(4);

	fprintf(stderr,"Error in calloc name_to_save\n");
	exit(-1);
    }
    //strcpy(name_to_save, argv[1]);
    //files.name_to_save = g_strdup_printf("./spks/%s", argv[1]);
	
    while ( (opt = getopt(argc, argv, "o:t:r:d:g:")) != -1 ) {
      switch (opt) {
      case 'o':
	  if (strlen(optarg) >= 128) {
	      fprintf(stderr, "Filename should be shroter\n");
	      FREE_DATA(4);
	      exit(-1);
	  }

	  mkdir("./spks", 0777);
	  files.name_to_save = g_strdup_printf("./spks/%s", optarg);
                
	  break;
      case 't':
	  acq_time = check_positive_int(optarg);
	  if (acq_time < 0) {
	      fprintf(stderr, "-t flag error: acquistion time should be non-negative\n");
	      FREE_DATA(4);
	      FREE_FILENAME_TO_SAVE_STR();
	      exit(-1);
	  }

	  break;
      case 'r':
	  x = check_positive_int(optarg);
	  if (x < 0) {
	      fprintf(stderr, "-r flag error: range should be non-negative\n");
	      FREE_DATA(4);
	      FREE_FILENAME_TO_SAVE_STR();
	      exit(-1);
	  }

	  for (i = 0; i < 4; i++) {
	      rangeset[i] = x;
	  }
                
	  break;
      case 'd':
	  delay = check_positive_int(optarg);
	  if (delay < 0) {
	      fprintf(stderr, "-d flag error: delay should be non-negative\n");
	      FREE_DATA(4);
	      FREE_FILENAME_TO_SAVE_STR();
	      exit(-1);
	  }
  
	  break;
      case 'g':
	  goodness_lim = check_positive_int(optarg);
	  if ((goodness_lim != 0) && (goodness_lim != 2) && (goodness_lim != 3)) {
	      fprintf(stderr, "-g flag error (goodness_lim == %d): -g GOODNESS_LIM (=0 write all signals, =2 write if 2 pairs of coinc, =3 do not write signals\n", goodness_lim);
	      FREE_DATA(4);
	      FREE_FILENAME_TO_SAVE_STR();
	      exit(-1);
	  }
            
	  break;
      default:
	  abort();
      }
    }
    
    printf("File to save: %s\n", files.name_to_save);
    
	
    GtkWidget *graph_area1, *graph_area2, *graph_area3, *graph_area4, *main_hbox, *vbox_graph, *button_vbox, *button_read, *button_write;
    GtkWidget *button_reset,  *button_coinc_on, *button_coinc_off, *button_delay, *button_test, *button_save_to, *button_read_once;
    GtkWidget *table_button, *table_readfile, *table_button_zoom;
    GtkWidget *scale_zoom, *button_rghtzoom, *button_leftzoom, *button_yzoom, *button_unzoom, *button_yup, *button_ydown;
    GtkAdjustment *adjust_scale;
    GtkWidget *hr1, *hr2;
    GtkWidget *button_readfile, *button_readfile_next, *image_readfile_next, *button_readfile_prev, *image_readfile_prev;
    GtkWidget *view;
    GtkTextBuffer *buffer_calc;
    gchar *tempstr;
	
    ret = cyusb_open();
    if (ret < 0) {
	perror("Error opening library\n");
	FREE_DATA(4);
	FREE_FILENAME_TO_SAVE_STR();

	exit(EXIT_FAILURE);
    }
    else if (ret == 0) {
	perror("No device found!\n");
    }
    else if (ret > 1) {
	perror("More than 1 devices of interest found. Disconnect unwanted devices\n");      
    }

    if (ret > 0) {
	h = cyusb_gethandle(0);
	if (cyusb_getvendor(h) != 0x04b4) {
	    perror("Cypress chipset not detected\n");
	    FREE_DATA(4);
	    FREE_FILENAME_TO_SAVE_STR();
	    cyusb_close();

	    exit(EXIT_FAILURE);
	}

	ret = cyusb_kernel_driver_active(h, 0);
	if (ret != 0) {
	    perror("kernel driver active. Exitting\n");
	    FREE_DATA(4);
	    FREE_FILENAME_TO_SAVE_STR();
	    cyusb_close();

	    exit(EXIT_FAILURE);
	}

	ret = cyusb_claim_interface(h, 0);
	if (ret != 0) {
	    perror("Error in claiming interface\n");
	    FREE_DATA(4);
	    FREE_FILENAME_TO_SAVE_STR();
	    cyusb_close();

	    exit(EXIT_FAILURE);
	}
	cyusb_clear_halt(h, OUT_EP);
    }
	
    gtk_init (&argc, &argv);

    main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (main_window), "Read data from PLIS12cy - 4Det");
    g_signal_connect (main_window, "destroy", G_CALLBACK (gtk_main_quit), NULL);
    //gtk_window_set_resizable(GTK_WINDOW (main_window), TRUE);

    main_statusbar = gtk_statusbar_new();
    files.sb_context_id = gtk_statusbar_get_context_id(GTK_STATUSBAR (main_statusbar), "Statusbar info");
    tempstr = g_strdup_printf("File name to save: %s", files.name_to_save);
    gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), files.sb_context_id, tempstr);
    g_free(tempstr);
    tempstr = NULL;

    coord_statusbar = gtk_statusbar_new();

    main_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add (GTK_CONTAINER (main_window), main_hbox);

    vbox_graph = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(main_hbox), vbox_graph, TRUE, TRUE, 0);
	
    graph_area1 = gtk_drawing_area_new ();
    gtk_widget_add_events(graph_area1, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_size_request (graph_area1, 500, 150);
    g_signal_connect (graph_area1, "draw",
		      G_CALLBACK (graph1_callback), (gpointer) data);
    g_signal_connect(graph_area1, "button-press-event", 
		     G_CALLBACK(clicked_on_ga), NULL);
    g_signal_connect (graph_area1, "motion-notify-event",
		      G_CALLBACK (motion_in_ga), GINT_TO_POINTER(0)); 
    g_signal_connect (graph_area1, "button-release-event",
		      G_CALLBACK (unclicked_on_ga), NULL);
    g_signal_connect (graph_area1, "configure-event",
		      G_CALLBACK (graph_configure_event), NULL); 
    gtk_box_pack_start(GTK_BOX(vbox_graph), graph_area1, TRUE, TRUE, 1);
	
    graph_area2 = gtk_drawing_area_new ();
    gtk_widget_add_events(graph_area2, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_size_request (graph_area2, 500, 150);
    g_signal_connect (G_OBJECT (graph_area2), "draw",
		      G_CALLBACK (graph2_callback), (gpointer) data);
    g_signal_connect (graph_area2, "motion-notify-event",
		      G_CALLBACK (motion_in_ga), GINT_TO_POINTER(1)); 
    gtk_box_pack_start(GTK_BOX(vbox_graph), graph_area2, TRUE, TRUE, 1);
	
    graph_area3 = gtk_drawing_area_new ();
    gtk_widget_add_events(graph_area3, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_size_request (graph_area3, 500, 150);
    g_signal_connect (G_OBJECT (graph_area3), "draw",
		      G_CALLBACK (graph3_callback), (gpointer) data);
    gtk_box_pack_start(GTK_BOX(vbox_graph), graph_area3, TRUE, TRUE, 1);
	
    graph_area4 = gtk_drawing_area_new ();
    gtk_widget_add_events(graph_area4, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    gtk_widget_set_size_request (graph_area4, 500, 150);
    g_signal_connect (G_OBJECT (graph_area4), "draw",
		      G_CALLBACK (graph4_callback), (gpointer) data);
    gtk_box_pack_start(GTK_BOX(vbox_graph), graph_area4, TRUE, TRUE, 1);
	
    gtk_box_pack_start(GTK_BOX(vbox_graph), main_statusbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox_graph), coord_statusbar, FALSE, FALSE, 0);
		
    button_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(main_hbox), button_vbox, FALSE, FALSE, 0);
	
     
    button_read = gtk_button_new_with_label("Read data");
    g_signal_connect (G_OBJECT (button_read), "clicked",
		      G_CALLBACK (button_read_cb), (gpointer) data);
	
    button_write = gtk_button_new_with_label("Write data");
    g_signal_connect (G_OBJECT (button_write), "clicked",
		      G_CALLBACK (button_write_cb), (gpointer) NULL);
	
    for (i = 0; i < 4; i++) {
	tempstr = g_strdup_printf("Set threshold #%d", i+1);
	button_set_range[i] = gtk_button_new_with_label(tempstr);
	g_signal_connect (G_OBJECT (button_set_range[i]), "clicked",
			  G_CALLBACK (button_range_cb), (gpointer) NULL);
	g_free(tempstr);
	tempstr = NULL;
		
	entry_range[i] = gtk_entry_new();
	tempstr = g_strdup_printf("%d", rangeset[i]);
	gtk_entry_set_text(GTK_ENTRY(entry_range[i]), tempstr);
	g_free(tempstr);
	tempstr = NULL;
    }
	
    button_reset = gtk_button_new_with_label("Reset");
    g_signal_connect (G_OBJECT (button_reset), "clicked",
		      G_CALLBACK (button_reset_cb), (gpointer) NULL);
	
    button_test = gtk_button_new_with_label("Тест");
    g_signal_connect (G_OBJECT (button_test), "clicked",
		      G_CALLBACK (button_test_cb), (gpointer) NULL);
	
    button_delay = gtk_button_new_with_label("Set delay");
    g_signal_connect (G_OBJECT (button_delay), "clicked",
		      G_CALLBACK (button_delay_cb), (gpointer) NULL);
    entry_delay = gtk_entry_new();
    tempstr = g_strdup_printf("%d", delay);
    gtk_entry_set_text(GTK_ENTRY(entry_delay), tempstr);
    g_free(tempstr);
    tempstr = NULL;
    //    send_command("w", delay);
    //    send_command("r", 0);
	
    button_save_to = gtk_button_new_with_label("Save to file");
    /*	g_signal_connect (G_OBJECT(button_save_to), "clicked",
	G_CALLBACK(button_save_to_cb), (gpointer) data);*/

    button_read_once = gtk_button_new_with_label("Read once");
    g_signal_connect (G_OBJECT(button_read_once), "clicked",
		      G_CALLBACK(button_read_once_cb), (gpointer) data);
			
    button_coinc_on = gtk_button_new_with_label("Coinc on");
    g_signal_connect (G_OBJECT(button_coinc_on), "clicked",
		      G_CALLBACK(button_coinc_on_cb), (gpointer) data);
	
    button_coinc_off = gtk_button_new_with_label("Coinc off");
    g_signal_connect (G_OBJECT(button_coinc_off), "clicked",
		      G_CALLBACK(button_coinc_off_cb), (gpointer) data);
	
    label_intens = gtk_label_new("");
    tempstr = g_strdup_printf("%d cts/s | 0 s", intens);
    gtk_label_set_text(GTK_LABEL(label_intens), tempstr);
    g_free(tempstr);
    tempstr = NULL;
	
    adjust_scale = gtk_adjustment_new(SIZEOF_DATA(FILETYPE)/2 + 1.0, 25.0, SIZEOF_DATA(FILETYPE)/2+1.0, 1, 1.0, 1.0);
    scale_zoom = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjust_scale);
    gtk_scale_set_value_pos (GTK_SCALE (scale_zoom), GTK_POS_LEFT);
    gtk_scale_set_digits (GTK_SCALE (scale_zoom), 0);
    g_signal_connect (G_OBJECT(adjust_scale), "value-changed",
		      G_CALLBACK (scale_zoom_changed), NULL);
	
    button_leftzoom = gtk_button_new_with_label("<");
    g_signal_connect (G_OBJECT(button_leftzoom), "clicked",
		      G_CALLBACK(left_zoom_cb), NULL);
			
    button_rghtzoom = gtk_button_new_with_label(">");
    g_signal_connect (G_OBJECT(button_rghtzoom), "clicked",
		      G_CALLBACK(right_zoom_cb), NULL);
			
    button_yzoom = gtk_button_new_with_label("+Y");
    g_signal_connect (G_OBJECT(button_yzoom), "clicked",
		      G_CALLBACK(yzoom_cb), NULL);
						
    button_unzoom = gtk_button_new_with_label("0");
    g_signal_connect (G_OBJECT(button_unzoom), "clicked",
		      G_CALLBACK(unzoom_cb), adjust_scale);
            
    button_yup = gtk_button_new_with_label("UP");
    g_signal_connect (G_OBJECT(button_yup), "clicked",
		      G_CALLBACK(yup_cb), NULL);
    
    button_ydown = gtk_button_new_with_label("DOWN");
    g_signal_connect (G_OBJECT(button_ydown), "clicked",
		      G_CALLBACK(ydown_cb), NULL);
	
    hr1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
			
	
    table_button = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(table_button), TRUE);
    gtk_grid_attach(GTK_GRID(table_button), button_read, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), button_write, 1, 0, 1, 1);
	
    for (i = 0; i < 4; i++) {
	gtk_grid_attach(GTK_GRID(table_button), button_set_range[i], 1, i+1, 1, 1);
	gtk_grid_attach(GTK_GRID(table_button), entry_range[i], 0, i+1, 1, 1);
    }
    
    gtk_grid_attach(GTK_GRID(table_button), entry_delay, 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), button_delay, 1, 5, 1, 1);
	
    gtk_grid_attach(GTK_GRID(table_button), button_reset, 0, 6, 1, 1);
    
    gtk_grid_attach(GTK_GRID(table_button), button_coinc_on, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), button_coinc_off, 1, 7, 1, 1);
	
    gtk_grid_attach(GTK_GRID(table_button), label_intens, 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), button_save_to, 0, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), button_read_once, 1, 8, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button), scale_zoom, 0, 9, 2, 1);
	
    table_button_zoom = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(table_button_zoom), TRUE);
    gtk_grid_attach(GTK_GRID(table_button), table_button_zoom, 0, 10, 2, 2);
    
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_yzoom, 0, 10, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_yup, 1, 10, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_unzoom, 2, 10, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_leftzoom, 0, 11, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_ydown, 1, 11, 1, 1);
    gtk_grid_attach(GTK_GRID(table_button_zoom), button_rghtzoom, 2, 11, 1, 1);
    
    gtk_box_pack_start(GTK_BOX(button_vbox), table_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(button_vbox), hr1, FALSE, FALSE, 2);
	
	
    view = gtk_text_view_new ();
    buffer_calc = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    gtk_text_buffer_set_text (buffer_calc, "Calc results:\n\nE = \nT = \nᐃT = \n", -1);
    gtk_text_buffer_create_tag (buffer_calc, "blue_foreground",
				"foreground", "blue", NULL);
    gtk_text_buffer_create_tag (buffer_calc, "red_foreground",
				"foreground", "red", NULL);
    gtk_text_buffer_create_tag (buffer_calc, "green_foreground",
				"foreground", "green", NULL);
	
    button_readfile = gtk_button_new_with_label("Read file");
    g_signal_connect (G_OBJECT(button_readfile), "clicked",
		      G_CALLBACK(button_readfile_cb), (gpointer) buffer_calc);
	
    button_readfile_next = gtk_button_new();
    image_readfile_next = gtk_image_new_from_file("./go-next-rtl.png");
    gtk_button_set_image(GTK_BUTTON(button_readfile_next), image_readfile_next);
    g_signal_connect (G_OBJECT(button_readfile_next), "clicked",
		      G_CALLBACK(button_readfile_next_cb), (gpointer) buffer_calc);
			
    button_readfile_prev = gtk_button_new();
    image_readfile_prev = gtk_image_new_from_file("./go-previous-ltr.png");
    gtk_button_set_image(GTK_BUTTON(button_readfile_prev), image_readfile_prev);
    g_signal_connect (G_OBJECT(button_readfile_prev), "clicked",
		      G_CALLBACK(button_readfile_prev_cb), (gpointer) buffer_calc);
	
	
    table_readfile = gtk_grid_new();
    gtk_grid_attach(GTK_GRID(table_readfile), button_readfile, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table_readfile), button_readfile_next, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table_readfile), button_readfile_prev, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table_readfile), view, 0, 1, 4, 2);

    gtk_box_pack_start(GTK_BOX(button_vbox), table_readfile, FALSE, FALSE, 0);

	
    gtk_widget_show_all (main_window);

    gtk_main ();

    FREE_DATA(4);
    FREE_FILENAME_TO_SAVE_STR();
    cyusb_close();

    return 0;
}

// 1 и 2ой разы считывать
