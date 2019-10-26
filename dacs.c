
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <error.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include "zaptel.h"

#define FALSE 0
#define TRUE (!FALSE)

#define BLOCK_LEN   160
#define MAX_BLOCK   16

#define DEBUGKD
//#define DEBUGKD_RW
#define FXS	1
#define FXO	2
char *EVENT[] = {
    "ZT_EVENT_NONE",
    "ZT_EVENT_ONHOOK ",
    "ZT_EVENT_RINGOFFHOOK ",
    "ZT_EVENT_WINKFLASH ",
    "ZT_EVENT_ALARM	",
    "ZT_EVENT_NOALARM ",
    "ZT_EVENT_ABORT ",
    "ZT_EVENT_OVERRUN ",
    "ZT_EVENT_BADFCS ",
    "ZT_EVENT_DIALCOMPLETE	",
    "ZT_EVENT_RINGERON ",
    "ZT_EVENT_RINGEROFF ",
    "ZT_EVENT_HOOKCOMPLETE ",
    "ZT_EVENT_BITSCHANGED ",
    "ZT_EVENT_PULSE_START ",
    "ZT_EVENT_TIMER_EXPIRED	",
    "ZT_EVENT_TIMER_PING",
    "ZT_EVENT_POLARITY  ",
    "ZT_EVENT_RINGBEGIN  ",
    "ZT_EVENT_EC_DISABLED ",
    "ZT_EVENT_REMOVED   "
};

char *TONE[] = {
    " ZT_TONE_STOP		",
    " ZT_TONE_DIALTONE	",
    " ZT_TONE_BUSY		",
    " ZT_TONE_RINGTONE	",
    " ZT_TONE_CONGESTION	",
    " ZT_TONE_CALLWAIT	",
    " ZT_TONE_DIALRECALL	",
    " ZT_TONE_RECORDTONE	",
    " ZT_TONE_INFO		",
    " ZT_TONE_CUST1		",
    " ZT_TONE_CUST2		",
    " ZT_TONE_STUTTER	",
    " ZT_TONE_MAX		"
};

char *HOOK[] = {
    "ZT_ONHOOK",
    "ZT_OFFHOOK",
    "ZT_WINK",
    "ZT_FLASH",
    "ZT_START",
    "ZT_RING",
    "ZT_RINGOFF"
};

typedef struct
{
    int id;
    int fd;
    int sig;
    int event;
    int hook;
    int set_hook;
    int e_num;
} Channel;

int cid=0;
Channel c[2];

    int
set_hook (Channel * c, int hook)
{
    int x, res;


    do
    {
        x = hook;
        res = ioctl (c->fd, ZT_HOOK, &x);
        if (res)
        {
            switch (errno)
            {
                case EBUSY:
                case EINTR:
                    /* Wait just in case */
                    usleep (10000);
                    continue;
                case EINPROGRESS:
                    res = 0;
                    break;
                default:
                    printf ("Couldn't set hook: %s\n", strerror (errno));
                    res = 0;
            }
        }
    }
    while (res);
#ifdef DEBUGKD
    if (res == -1)
        printf ("Unset hook %s in channel(%d)\n", HOOK[hook], c->id);
    else
        printf ("Set hook %s in channel(%d)\n", HOOK[hook], c->id);

#endif
    c->set_hook = hook;
    if(hook==ZT_ONHOOK)
        cid=0;
    return res;

}

int
set_tone (Channel * c, int tone)
{
    int x, res;
    if ((tone < ZT_TONE_STOP) || (tone > ZT_TONE_MAX))
    {
        printf ("tone range is uncorrect\n");
        return -1;
    }
    res = tone_zone_play_tone (c->fd, tone);

#ifdef DEBUGKD
    if (res)
        printf ("UnSet tone %s in channel(%d)\n", TONE[tone + 1], c->id);
    else
        printf ("Set tone %s in channel(%d)\n", TONE[tone + 1], c->id);
#endif
    return res;

}


void
set_echo (Channel * c, int echo)
{
    int x;
    int res;


    x = echo;
    res = ioctl (c->fd, ZT_ECHOCANCEL, &x);
    if (res)
        printf ("Unable to set echo cancellation to %d on channel %d\n", echo, c->id);
#ifdef DEBUGKD
    else
        printf ("set echo cancellation to %d on channel %d\n", echo, c->id);
#endif

}

void
set_train (Channel * c, int train)
{
    int x;
    int res;

    x = train;
    res = ioctl (c->fd, ZT_ECHOTRAIN, &x);
    if (res)
        printf ("Unable to set echo training to %d on channel %d\n",
                train, c->id);
#ifdef DEBUGKD
    else
        printf ("set echo traning to %d on channel %d\n", train, c->id);
#endif

}

    int
ring_phone (Channel * c)
{
    int x;
    int res;
    /* Make sure our transmit state is on hook */
    x = 0;
    /* x = ZT_ONHOOK; */
    /* res = ioctl (c->fd, ZT_HOOK, &x); */
    do
    {
        x = ZT_RING;
        res = ioctl (c->fd, ZT_HOOK, &x);
        if (res)
        {
            switch (errno)
            {
                case EBUSY:
                case EINTR:
                    /* Wait just in case */
                    usleep (10000);
                    continue;
                case EINPROGRESS:
                    res = 0;
                    break;
                default:
                    printf ("Couldn't ring the phone: %s\n", strerror (errno));
                    res = 0;
            }
        }
    }
    while (res);
    c->set_hook = ZT_RING;
    while (get_event(c)!=ZT_EVENT_RINGEROFF)
        usleep(1000);
#ifdef DEBUGKD
    printf ("set RING in channel %d\n", c->id);
#endif
    return res;
}

int
get_event (Channel * c)
{
    int event = 0;
    int res;
    int x;

    res = ioctl (c->fd, ZT_GETEVENT, &event);
    if (res == -1)
        return -1;
#ifdef DEBUGKD
    if (event)
        printf ("(%d): %s \n", c->id, EVENT[event]);
#endif
    c->event = event;
    return event;
}

static int
channel_open (Channel * c)
{
    struct zt_bufferinfo bi;
    struct zt_gains g;
    int x, fd, i;
    int linear, law;
    ZT_PARAMS zp;

    if ((fd = open ("/dev/zap/channel", O_RDWR | O_NONBLOCK)) < 0)
        return -1;

    if (ioctl (fd, ZT_SPECIFY, &(c->id)))
    {
        close (fd);
        return -1;
    }
    if (ioctl (fd, ZT_GET_BUFINFO, &bi) < 0)
    {
        close (fd);
        return -1;
    }
    /* bi.txbufpolicy = ZT_POLICY_IMMEDIATE; */
    /* bi.rxbufpolicy = ZT_POLICY_IMMEDIATE; */
    /* bi.numbufs = 4; */
    bi.bufsize = BLOCK_LEN;
    if (ioctl (fd, ZT_SET_BUFINFO, &bi) < 0)
    {
        close (fd);
        return -1;
    }
    if (ioctl (fd, ZT_CHANNO, &(c->id)))
    {
        close (fd);
        return -1;
    }
    /* Set default gains */
    if(c->id<9){

        g.chan = 0;
        for (i = 0; i < 256; i++)
        {
            g.rxgain[i] = i;
            g.txgain[i] = i;
        }
        if (ioctl (fd, ZT_SETGAINS, &g) < 0)
        {
            close (fd);
            return -1;
        }


    }
    law = ZT_LAW_DEFAULT;
    if (ioctl (fd, ZT_SETLAW, &law))
        return -1;
    linear = 0;
    if (ioctl (fd, ZT_SETLINEAR, &linear))
        return -1;

    c->fd = fd;

    set_hook(c,ZT_ONHOOK);
    x = 4000;
    ioctl(c->fd,ZT_ONHOOKTRANSFER,&x);

#ifdef DEBUGKD
    int ctl;
    ctl = open ("/dev/zap/ctl", O_RDWR);
    if (ctl < 0)
    {
        fprintf (stderr, "Unable to open /dev/zap/ctl: %s\n", strerror (errno));
        exit (1);
    }

    memset (&zp, 0, sizeof (zp));
    zp.channo = c->id;
    ioctl (ctl, ZT_GET_PARAMS, &zp);
    close (ctl);

    switch (zp.sigtype)
    {
        case ZT_SIG_FXOKS:
        case ZT_SIG_FXOLS:
        case ZT_SIG_FXOGS:
            c->sig = FXO;
            printf ("Open Channel %d with Signal FXO\n", c->id);
            break;
        case ZT_SIG_FXSKS:
        case ZT_SIG_FXSLS:
        case ZT_SIG_FXSGS:
            c->sig = FXS;
            printf ("Open Channel %d with Signal FXS\n", c->id);
            break;
        default:
            printf ("Channel %d Signal %d\n", c->id, c->sig);
            printf ("This program no implement for other signalling! Exit.\n");
            c->fd = -1;
            return -1;
            break;
    }
#endif
    return 0;
}

void
get_cid (Channel * c1, Channel * c2)
{

    struct pollfd pfd[1];
    int x,n,res,r=0, i=0,j=0,w=0,start;
    unsigned char buf[BLOCK_LEN + 1];
#ifdef DEBUGKD
    printf("Get cid from :%d\n",c1->id);
    printf("Hook of %d is %s\n",c2->id,HOOK[c2->set_hook]);
#endif

    set_hook(c2,ZT_ONHOOK);

    ioctl(c2->fd, ZT_SETCADENCE,NULL);
    ring_phone(c2);
    usleep(300000);

    for (i = 0,start=0;start<2;i++)
    {


        n = read (c1->fd, buf, BLOCK_LEN);      
        while ((n == -1) && (errno == EAGAIN))
        {
            usleep (5000);
            n = read (c1->fd, buf, BLOCK_LEN);
        }
        if (n >= 0)
        {
            if(n<BLOCK_LEN){
#ifdef DEBUGKD

                printf("get event in cid process!(read %d byte,write %d byte)(loop %d)\n",r,w,i);
#endif
                break;
            }
            if(n>0){
                if((buf[0]!=0x7f)&&(buf[1]!=0x7f)&&(buf[2]!=0x7f)){
                    if(start==0)
                        start = 1; 	/* in cid */
                }
                else {
                    if(start==1)
                        start = 2; 	/* after cid */
                }
            }
            r += n ;
            j = 0;
            while (j < n) {
                res = write(c2->fd, buf, n);
                if (res <= 0) {
                    if (errno == EAGAIN){
                        usleep(1000);
                    }
                    else if(errno==ELAST){
                        if(get_event(c2)!=-1){
                            if(c2->event == ZT_EVENT_ONHOOK)
                                break;
                        }
                    }
                    else
                    {
                        perror("write-..");
                        printf("exit cid with error\n");
                        break;
                    }
                }
                if(res>0){
                    j += res;
                    w += res;
                }
            }
        }
        else if(n==-1){
            if(errno != ELAST){
#ifdef DEBUGKD
                perror ("read");
                printf("exit cid with error\n");
#endif
                break;
            }
            else{
                if(get_event(c1)!=-1){
                    if(c1->event == ZT_EVENT_ONHOOK)
                        break;
                }
            }
        }
    }
    usleep(300000);

#ifdef DEBUGKD
    printf("read %d byte,write %d byte(loop %d)\n",r,w,i);
    printf("exit cid\n");
#endif
}

int
bridge_handle_event (Channel * c1, Channel * c2)
{
    int res = 0;
    if (c1->event == ZT_EVENT_WINKFLASH)
    {
        set_hook (c2, ZT_FLASH);
        res = 0;
    }
    if (c1->event == ZT_EVENT_ONHOOK)
    {
        printf ("(%d) get ONHOOK\n", c1->id);
        if (c2->sig == FXO)
        {
            set_tone (c2, ZT_TONE_CONGESTION);
        }
        else
        {
            set_tone (c2, ZT_TONE_STOP);
            set_hook (c2, ZT_ONHOOK);
        }
        res = 1;
    }

    return res;
}

int
bridge ()
{
    struct pollfd po[2];
    int res, e,src,dst,read_len;
    unsigned char buf[BLOCK_LEN +1];

    set_echo (&c[0], 1);
    set_train (&c[0], 1);
    set_echo (&c[1], 1);
    set_train (&c[1], 1);
    po[0].fd = c[0].fd;
    po[0].events = POLLIN | POLLPRI;
    po[1].fd = c[1].fd;
    po[1].events = POLLIN | POLLPRI;
    printf ("Bridge start %d<->%d\n",c[0].id,c[1].id);  
    for (;;)
    {
        po[0].revents = 0;
        po[1].revents = 0;

        src = poll (po, 2, 1000); // infinit wait: -1
        if (src == 0){ // timeout
            usleep(1000);
            printf("t");
            continue;
        }
        if (src <= -1) // error ocur
        {
            perror ("Poll:");
            src = -1;
            break;
        }
        src--;
        dst = (src==0)?1:0;
        if(po[src].revents & (POLLERR|POLLHUP|POLLNVAL)){
            perror ("Poll:");
            continue;
        }
        if(!(po[src].revents & (POLLIN|POLLPRI))){
            //	printf("evx");
            usleep(2000);
            continue;
        }
        read_len = read (c[src].fd, buf, BLOCK_LEN);
        while((read_len==-1) && (errno==EAGAIN)){
            printf("r%d:%d]",c[src].id,read_len);
            usleep(5000);
            read_len = read (c[src].fd, buf, BLOCK_LEN);
        }

        if( (read_len != BLOCK_LEN) ){ // get event
            if(get_event (&c[src])!=-1){
                res = bridge_handle_event(&c[src],&c[dst]);      
                if(c[src].event == ZT_EVENT_ONHOOK)
                    break;
            }

        }
        if(read_len>=0){
            /* for(;;){ */
            res = write(c[dst].fd,buf,read_len);
            if(res==-1){
                if(errno==ELAST){
                    if(get_event (&c[dst])!=-1){
                        res = bridge_handle_event(&c[dst],&c[src]);
                        if(c[dst].event == ZT_EVENT_ONHOOK)
                            break;
                    }
                }
            }

        }

        } /* for */

        printf("Bridge end %d<->%d\n",c[0].id,c[1].id);

        set_train (&c[0], 0);
        set_train (&c[1], 0);
        set_echo (&c[0], 0);
        set_echo (&c[1], 0);

        c[0].set_hook = 0;	
        c[1].set_hook = 0;
        return 0;

    }

int
dial (Channel * c)
{
    int res;
    struct zt_dialoperation zo = {
        .op = ZT_DIAL_OP_REPLACE,
        .dialstr = "T1w",
    };

            res = ioctl (c->fd, ZT_DIAL, &zo);
            if (res)
            {
                printf ("Error in dial: %s\n", strerror (errno));
                return -1;
            }
            else
            {
#ifdef DEBUGKD
                printf ("Dial in channel(%d)\n", c->id);
#endif
                return 0;
            }

}


int
test_offhook (Channel * c)
{
    struct zt_params par;
    memset (&par, 0, sizeof (par));
    if (ioctl (c->fd, ZT_GET_PARAMS, &par) != -1)
    {
        if (par.rxisoffhook)
            return 1;
    }
    return 0;

}

int main (int argc, char *argv[])
{
    int f, x, fdt;
    int ctl;
    int Event;
    int res = 0;
    int BS = BLOCK_LEN;
    unsigned long time_count, fxs_offhook;
    struct zt_params zp;
    char buf[BLOCK_LEN + 1];

            c[0].id = 1;
            c[1].id = 9;
            if (argc != 2)
            {
                printf ("Usage: %s <chan_num> \n", argv[0]);
                printf ("\t\t Create dacs between chan_num <--> chan_num+8\n");
                return -1;
            }
            c[0].id = atoi (argv[1]);
            if ((c[0].id > 8) || (c[0].id < 1))
            {
                printf ("Invalid Channel Number: it must be in range:  1..8\n");
                return -1;
            }
            c[1].id = c[0].id + 8;

            if (channel_open (&c[0]) == -1)
            {
                perror ("Channel open 1");
                return -1;
            }
            if (channel_open (&c[1]) == -1)
            {
                perror ("Channel open 2");
                return -1;
            }

            if (c[0].fd == -1 || c[1].fd == -1)
            {
                printf ("Error in channel open \n");
                close (c[0].fd);
                close (c[1].fd);
                return 0;
            }
            int i = 0, j = 1;

            /* set_echo (&c[0], 0); */
            /* set_train (&c[0], 0); */
            /* set_echo (&c[1], 0); */
            /* set_train (&c[1], 0); */

            c[i].hook = c[j].hook = 0;
            set_hook (&c[0], ZT_ONHOOK);    
            set_hook (&c[1], ZT_ONHOOK);    
            time_count = 0;
            for (;;)			/* main loop for port handelling */
            {

                i = (i == 0) ? 1 : 0;
                j = (j == 0) ? 1 : 0;
                usleep (100000);		/* 0.1 sec interval for port checking */
                time_count++;
                if (time_count > 2000000000)
                    time_count = 0;

                if (get_event (&c[i]) == -1)
                    continue;

                x = ZT_FLUSH_READ | ZT_FLUSH_WRITE;
                res = ioctl (c[i].fd, ZT_FLUSH, &x);

                switch (c[i].event)
                {
                    case ZT_EVENT_ONHOOK:
                        set_tone (&c[i], ZT_TONE_STOP);
                        set_hook (&c[i], ZT_ONHOOK);
                        if (c[j].sig == FXO)
                            //                                  set_tone (&c[j], ZT_TONE_BUSY);
                            set_tone (&c[j], ZT_TONE_CONGESTION);
                        else
                        {
                            set_tone (&c[j], ZT_TONE_STOP);
                            set_hook (&c[j], ZT_ONHOOK);
                        }
                        break;
                    case ZT_EVENT_WINKFLASH:
                        set_hook (&c[j], ZT_FLASH);
                        break;
                    case ZT_EVENT_RINGBEGIN:
                        if ((c[i].sig == FXS)&&(c[j].set_hook!=ZT_RING)){
                            get_cid (&c[i], &c[j]); 
                        }

                        break;
                    case ZT_EVENT_RINGERON:
                        if ((time_count - fxs_offhook > 20) && (!test_offhook (&c[j])))
                            if (c[i].sig == FXO){
                                set_hook (&c[i], ZT_ONHOOK);
                                set_hook (&c[j], ZT_ONHOOK);
                            }
                        break;
                    case ZT_EVENT_HOOKCOMPLETE:

                        break;
                    case ZT_EVENT_DIALCOMPLETE:
                        bridge ();
                        break;
                    case ZT_EVENT_RINGOFFHOOK:
                        c[i].hook = 1;
                        switch (c[i].sig)
                        {
                            case FXO:
                                c[i].set_hook = 0;

                                set_hook (&c[i], ZT_RINGOFF);
                                set_hook (&c[i], ZT_OFFHOOK);
                                break;
                            case FXS:
                                fxs_offhook = time_count;
                                break;
                        }
                        switch (c[j].sig)
                        {
                            case FXO:
                                set_hook (&c[j], ZT_START);
                                dial (&c[j]);
                                break;
                            case FXS:
                                set_hook (&c[j], ZT_RING);
                                set_hook (&c[j], ZT_OFFHOOK);
                                dial (&c[j]);
                                break;
                        }

                        break;
                    default:

                        break;

                }

                if (test_offhook (&c[i]) && test_offhook (&c[j]) &&
                        c[i].hook == 1 && c[j].hook == 1)
                {
                    bridge ();
                    c[i].hook = c[j].hook = 0;
                }


            }
            set_train (&c[0], 0);
            set_train (&c[1], 0);
            set_echo (&c[0], 0);
            set_echo (&c[1], 0);

            close (c[0].fd);
            close (c[1].fd);
            return 0;
        }

    /*
       FLASH
       -- Started three way call on channel 2
       -- Native bridging Zap/1-1 and Zap/2-1
       -- Started music on hold, class 'default', on Zap/1-1
       -- Native bridging Zap/1-1 and Zap/2-1
       -- Starting simple switch on 'Zap/2-1'

       -- Stopped music on hold on Zap/1-1
       -- Started music on hold, class 'default', on Zap/1-1
       -- Native bridging Zap/1-1 and Zap/2-1

*/
