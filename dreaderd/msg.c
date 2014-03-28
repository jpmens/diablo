#define FDPASS_DAMN_CMSG_MACROS        1

#include "defs.h"

Prototype int SendMsg(int to_fd, int send_fd, DnsRes *dres);
Prototype int RecvMsg(int from_fd, int *recv_fd, DnsRes *dres);

#if FDPASS_DAMN_CMSG_MACROS

/*
 * I generally don't like #ifdef defining entire functions, but
 * this was getting too damn messy.  Linux apparently wants you
 * to use some nifty new cmsg macros to assemble this sort of
 * message, and it wasn't too compatible with what used to live
 * here.  I've broken the SendMsg into two functions for now
 * so it is easier to see what is going on.  JG20050830
 */

int
SendMsg(int to_fd, int send_fd, DnsRes *dres)
{
    struct msghdr msg;
    struct iovec iov;
    char tbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
    int res = 0;
    int cmsgsize;
    int sent = 0;
         
    bzero(&msg, sizeof(msg));

    iov.iov_base = (void *)dres;
    iov.iov_len = sizeof(DnsRes);
                
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (send_fd >= 0) {
      cmsgsize = 1;
    } else {
      cmsgsize = 0;
    }
    msg.msg_control = tbuf;
    msg.msg_controllen = sizeof(tbuf);
    msg.msg_flags = 0;
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * cmsgsize);
    memcpy((int *)CMSG_DATA(cmsg), &send_fd, sizeof(int));
    msg.msg_controllen = cmsg->cmsg_len;

    errno = 0;
        
    /*
     * If we do not turn off nagle, lots of little 'group' transactions
     * can result in major delays even if nagle only kicks in on a few
     * of them.
     */
        
#ifdef TCP_NODELAY
    /*
     * Turn on TCP_NODELAY
     */
    if (send_fd >= 0) {
      int one = 1;
      setsockopt(send_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));
    }
#endif
     
    /*
     * Send the message.  This should never fail.  If it does, try to core
     * the reader subprocess.  Certainly kill it.
     */
        
    if (DebugOpt)
      printf("SendMsg() pid= %d fd=%d size=%d\n", (int)getpid(), send_fd, sizeof(DnsRes));
    while (!sent) {
      res = sendmsg(to_fd, &msg, 0);
      sent = 1;
      if (res < 0) {
          if (errno == EAGAIN)
              sent = 0;
          else
              logit(LOG_ERR, "sendmsg error (%s)", strerror(errno));
      }
    }
    return(res);
}


int
RecvMsg(int from_fd, int *recv_fd, DnsRes *dres)
{
    int result = 0;
    struct msghdr msg;
    struct iovec  iov;
    char tbuf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr *cmsg;
  
    bzero(&msg, sizeof(msg));
  
    iov.iov_base = (void *)dres;
    iov.iov_len = sizeof(DnsRes);
 
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    msg.msg_control = (caddr_t)tbuf;
    msg.msg_controllen = sizeof(tbuf);
    msg.msg_flags = 0;

    *recv_fd = -1;
    errno = 0;

    /*
     * receive message w/ file descriptor   
     */

    result = recvmsg(from_fd, &msg, MSG_EOR|MSG_WAITALL);
    if (result >= 0 || errno == EAGAIN) {
	if ((cmsg = CMSG_FIRSTHDR(&msg))) {
            if (! cmsg->cmsg_type == SCM_RIGHTS) {
	        logit(LOG_ERR, "recvmsg got unknown control message %d", cmsg->cmsg_type);
	        return(-1);
	    }
	    *recv_fd = *(int *)CMSG_DATA(cmsg);
	    if (DebugOpt && errno != EAGAIN)
	        printf("RecvMsg() pid=%d fd=%d size=%d\n",
					(int)getpid(), *recv_fd, result);
	} else {
	    logit(LOG_ERR, "recvmsg CMSG_FIRSTHDR null, msg_flags=%d", msg.msg_flags);
	}
    } else {
	logit(LOG_ERR, "recvmsg error (%s)", strerror(errno));
    }
    return(result);
}



#else /*FDPASS_DAMN_CMSG_MACROS*/




int
SendMsg(int to_fd, int send_fd, DnsRes *dres)
{
    struct msghdr msg;
    struct iovec iov;
    struct {
#if FDPASS_USES_CMSG
	struct cmsghdr cmsg;
#endif
	int fd;
    } cmsg;
    int res = 0;
    int cmsgsize;
    int sent = 0;
         
    bzero(&msg, sizeof(msg));
    bzero(&cmsg, sizeof(cmsg));

    iov.iov_base = (void *)dres;
    iov.iov_len = sizeof(DnsRes);
                
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (send_fd >= 0) {
	cmsgsize = sizeof(cmsg);
    } else {
#if FDPASS_USES_CMSG
	cmsgsize = sizeof(struct cmsghdr);
#else
	cmsgsize = 0;
#endif
    }
#if FDPASS_USES_ACC
    msg.msg_accrights = (caddr_t)&cmsg;
    msg.msg_accrightslen = cmsgsize;
#else
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = cmsgsize;
#endif
#if FDPASS_USES_CMSG
    msg.msg_flags = 0;
    cmsg.cmsg.cmsg_len = cmsgsize;
    cmsg.cmsg.cmsg_level = SOL_SOCKET;
    cmsg.cmsg.cmsg_type = SCM_RIGHTS;
#endif 
    cmsg.fd = send_fd;

    errno = 0;
        
    /*
     * If we do not turn off nagle, lots of little 'group' transactions
     * can result in major delays even if nagle only kicks in on a few
     * of them.
     */
        
#ifdef TCP_NODELAY
    /*
     * Turn on TCP_NODELAY
     */
    if (send_fd >= 0) {
	int one = 1;
	setsockopt(send_fd, IPPROTO_TCP, TCP_NODELAY, (void *)&one, sizeof(one));
    }
#endif
     
    /*
     * Send the message.  This should never fail.  If it does, try to core
     * the reader subprocess.  Certainly kill it.
     */
        
    if (DebugOpt)
	printf("SendMsg() pid= %d fd=%d size=%d\n", (int)getpid(), cmsg.fd, sizeof(DnsRes));
    while (!sent) {
	res = sendmsg(to_fd, &msg, 0);
	sent = 1;
	if (res < 0) {
	    if (errno == EAGAIN)
		sent = 0;
	    else
		logit(LOG_ERR, "sendmsg error (%s)", strerror(errno));
	}
    }
    return(res);
}

int
RecvMsg(int from_fd, int *recv_fd, DnsRes *dres)
{
    int result = 0;
    struct msghdr msg;
    struct iovec  iov;
    struct {
#if FDPASS_USES_CMSG
	struct cmsghdr cmsg;
#endif
	int fd;
    } cmsg;
  
    bzero(&msg, sizeof(msg));
  
    iov.iov_base = (void *)dres;
    iov.iov_len = sizeof(DnsRes);
 
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
#if FDPASS_USES_ACC
    msg.msg_accrights = (caddr_t)&cmsg;
    msg.msg_accrightslen = sizeof(cmsg);
#else
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = sizeof(cmsg);
#endif
#if FDPASS_USES_CMSG
    msg.msg_flags = 0;
    cmsg.cmsg.cmsg_len = sizeof(cmsg);
#endif
    cmsg.fd = -1;
    errno = 0;

    /*
     * receive message w/ file descriptor   
     */

    result = recvmsg(from_fd, &msg, MSG_EOR|MSG_WAITALL);
    if (result >= 0 || errno == EAGAIN) {
	*recv_fd = cmsg.fd;
	if (DebugOpt && errno != EAGAIN)
	    printf("RecvMsg() pid=%d fd=%d size=%d\n",
					(int)getpid(), cmsg.fd, result);
    } else {
	logit(LOG_ERR, "recvmsg error (%s)", strerror(errno));
    }
    return(result);
}
#endif /*FDPASS_DAMN_CMSG_MACROS*/
