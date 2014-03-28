
/*
 * DFA.C      - Direct File Access to articles
 *
 * (c)Copyright 2001, Francois Petillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"

#if USE_AIO
    #if USE_LINUX_KAIO
      #include <linux/aio.h>
    #else
      #include <aio.h>
    #endif
#include <signal.h>
#endif

Prototype DirectFileAccess* NewDFA(Connection *conn, int lf, int offset, int size);
Prototype void DelDFA(Connection *conn);
Prototype void NNSendLocalArticle(Connection *conn);

int NNCopyLocalArticle(Connection *conn, int rs);
void DFADataWrite(ServReq *sreq, const void *data, int len);
void NNSLA_Loop(Connection *conn);

#if USE_AIO
Prototype void AIOBlockSignal(void);
Prototype void AIOUnblockSignal(void);

int AIOBlockedSignal = 0;

void AIORDataRead(Connection *conn);
void Sig_AIO(int signo, siginfo_t *info, void *context);

struct sigaction* sig_aio = NULL;

#define AIOSIG        SIGUSR1

#endif

/* We are using a fifo to store unused DFA structure */
DirectFileAccess* dfa_Trash = NULL;

/* Alloc dfa struct */
DirectFileAccess*
NewDFA(Connection *conn, int lf, int offset, int size)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess* el=NULL;

#if USE_AIO
    if (!sig_aio) {
      sig_aio = (struct sigaction*) malloc(sizeof(struct sigaction)) ;
      sigemptyset(&sig_aio->sa_mask);
      sigaddset(&sig_aio->sa_mask, AIOSIG);
      sig_aio->sa_flags = SA_SIGINFO;
      sig_aio->sa_sigaction = Sig_AIO;
      if (sigaction(AIOSIG, sig_aio, NULL)) {
          logit(LOG_ERR, "NewDFA : cannot sigaction");
          exit(1);
      }
      AIOBlockSignal();
    }
#endif

    if (lf==-1) {
      logit(LOG_ERR, "NewDFA : fd is -1");
      return NULL;
    }

    if (dfa_Trash) {
      el = dfa_Trash ;
      dfa_Trash = el->dfa_Next ;
    } else {
      el = (DirectFileAccess*) malloc(sizeof(DirectFileAccess));
#if USE_AIO
      el->dfa_AIOcb = (struct aiocb*) malloc(sizeof(struct aiocb)) ;
#endif
    }

    if (!el) return NULL ;
    /* check end of article */
    if (lseek(lf, offset+size-1, SEEK_SET)!=(offset+size-1)) {
      logit(LOG_ERR, "NewDFA : cannot seek to the end of article");
      el->dfa_Next = dfa_Trash;
      dfa_Trash = el;
      return NULL ;
    }

#if USE_AIO
    el->dfa_AIOcb->aio_fildes = lf ;
    el->dfa_AIOcb->aio_offset = offset ;
    el->dfa_AIOcb->aio_nbytes = 0 ;
    el->dfa_AIOcb->aio_reqprio = 0 ;
    el->dfa_AIOcb->aio_buf = el->dfa_Buffer ;
    el->dfa_AIOcb->aio_sigevent.sigev_notify = SIGEV_SIGNAL ;
    el->dfa_AIOcb->aio_sigevent.sigev_signo = AIOSIG;
    el->dfa_AIOcb->aio_sigevent.sigev_value.sival_ptr = conn ;
#else
    if (lseek(lf, offset, SEEK_SET)!=offset) { /* bring it back home */
      logit(LOG_ERR, "NewDFA : cannot seek to the beginning of article");
      el->dfa_Next = dfa_Trash;
      dfa_Trash = el;
      return NULL ;
    }
    el->dfa_Fd     = lf ;
#endif

    conn->co_DirectFA = el ;

    el->dfa_Size   = size ;
    el->dfa_LF     = 0 ;
    el->dfa_Next   = NULL ;

    switch(conn->co_SReq->sr_CConn->co_ArtMode) {
      case COM_BODY :
      case COM_BODYWVF :
      case COM_BODYNOSTAT :
          el->dfa_InHdr = JMPBDY ;
          if (sreq->sr_Cache) {
              AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0) ;
              fclose(sreq->sr_Cache) ;
              sreq->sr_Cache = NULL ;
          }
          break ;
      case COM_HEAD :
          if (sreq->sr_Cache) {
              AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0) ;
              fclose(sreq->sr_Cache) ;
              sreq->sr_Cache = NULL ;
          }
      default :
          el->dfa_InHdr = 0 ;
    }

    return el;
}

void
DelDFA(Connection *conn)
{
    DirectFileAccess* el=conn->co_DirectFA ;
    conn->co_DirectFA = NULL ;
#if USE_AIO
    if (el->dfa_AIOcb->aio_fildes!=-1) {
      close(el->dfa_AIOcb->aio_fildes) ;
      el->dfa_AIOcb->aio_fildes = -1 ;
    }
#else
    if (el->dfa_Fd!=-1) {
      close(el->dfa_Fd) ;
      el->dfa_Fd = -1 ;
    }
#endif
    el->dfa_Next = dfa_Trash;
    dfa_Trash = el;
}

#if USE_AIO
/* Get a signal, read data */
void Sig_AIO(int signo, siginfo_t *info, void *context)
{
    Connection *conn = (Connection*) info->si_value.sival_ptr ;
    DirectFileAccess *dfa = conn->co_DirectFA;
    struct aiocb *aio=dfa->dfa_AIOcb;
    const struct aiocb *aiolist[1];
    int ret;

    /* check IO */
    aiolist[0] = aio;
    if ((ret = aio_error(aio))) {
      if (ret==EINPROGRESS) {
          logit(LOG_ERR, "SigAIOPoll : EINPROGRESS", errno);
          if (aio_suspend(aiolist,1,NULL)) {
              logit(LOG_ERR, "SigAIOPoll : aio_suspend failed (errno=%i)", errno);
              DelDFA(conn);
              NNServerTerminate(conn);
              return;
          } else {
              logit(LOG_NOTICE, "SigAIOPoll : aio_suspend on %i", aio->aio_fildes) ;
          }
      } else {
          logit(LOG_ERR, "SigAIOPoll : IO error %i", ret);
          DelDFA(conn);
          NNServerTerminate(conn);
      }
    }
    /* we can read the buffer */
    NNSLA_Loop(conn);
}

void
AIODataRead(Connection *conn)
{
    DirectFileAccess *dfa = conn->co_DirectFA;

    dfa->dfa_AIOcb->aio_nbytes = (dfa->dfa_Size>sizeof(dfa->dfa_Buffer)) ? sizeof(dfa->dfa_Buffer) : dfa->dfa_Size ;
    if (DebugOpt)
      printf("AIODataRead : fd %i sz %i of %i", dfa->dfa_AIOcb->aio_fildes, dfa->dfa_AIOcb->aio_nbytes, (int)dfa->dfa_AIOcb->aio_offset);

    if (aio_read(dfa->dfa_AIOcb)) {
      logit(LOG_ERR, "AIODataRead : error on reading (errno=%i)", errno);
      DelDFA(conn);
      NNServerTerminate(conn);
    }
}

void AIOBlockSignal(void)
{
    if (sig_aio)
      sigprocmask(SIG_BLOCK, &sig_aio->sa_mask, NULL) ;
}

void AIOUnblockSignal(void)
{
    if (sig_aio)
      sigprocmask(SIG_UNBLOCK, &sig_aio->sa_mask, NULL) ;
}

#endif

void
DFADataWrite(ServReq *sreq, const void *data, int len)
{
    if (sreq->sr_CConn)
      MBWrite(&sreq->sr_CConn->co_ArtBuf, data, len) ;
    if (sreq->sr_Cache)
      fwrite(data, 1, len, sreq->sr_Cache);
}

/* Reading buffer */
void
NNSLA_Loop(Connection *conn)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess *dfa = conn->co_DirectFA;
    int rs ;

    conn->co_Func = NNSendLocalArticle;
    conn->co_State = "getlclart";

#if USE_AIO
      rs = aio_return(dfa->dfa_AIOcb);        
#else
    do {
      int sizofbuf = sizeof(dfa->dfa_Buffer) ;
      rs = read(dfa->dfa_Fd, dfa->dfa_Buffer, (dfa->dfa_Size>sizofbuf) ? sizofbuf : dfa->dfa_Size) ;

      if (rs<0) {
          if (errno == EAGAIN) { /* retry later */
              if (DebugOpt)
                  printf("NNGetLocal2: retry later...") ;
              return;
          } else {
              /* ooops, real error */
              DelDFA(conn) ;
              NNServerTerminate(conn) ;
              return;
          }
      }
#endif

      switch (NNCopyLocalArticle(conn, rs)) {
          case BLOCK_OK :
              dfa->dfa_Size -= rs ;
              if (!dfa->dfa_Size) {
                  if (sreq->sr_CConn)
                          MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
                  if (sreq->sr_Cache) {
                      if (fflush(sreq->sr_Cache) || ferror(sreq->sr_Cache))
                          AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                      else
                          CommitCache(conn, 0);
                      fclose(sreq->sr_Cache) ;
                      sreq->sr_Cache = NULL ;
                  }
                  DelDFA(conn) ;
                  NNFinishSReq(conn, ".\r\n", 0) ;
                  return;
              }
              if (FastCopyOpt && sreq->sr_CConn) {
                  MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
              }
              break ;
          case BLOCK_END :
              if (sreq->sr_CConn)
                      MBCopy(&sreq->sr_CConn->co_ArtBuf, &sreq->sr_CConn->co_TMBuf);
              if (sreq->sr_Cache) {
                  AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                  fclose(sreq->sr_Cache) ;
                  sreq->sr_Cache = NULL ;
              }
              DelDFA(conn) ;
              NNFinishSReq(conn, ".\r\n", 0) ;
              return;
          case BLOCK_ERROR :
          default :
              DelDFA(conn);
              if (sreq->sr_Cache) {
                  AbortCache(fileno(sreq->sr_Cache), sreq->sr_MsgId, 0);
                  fclose(sreq->sr_Cache) ;
                  sreq->sr_Cache = NULL ;
              }
              NNServerTerminate(conn);
              return;
      }
#if USE_AIO
    dfa->dfa_AIOcb->aio_offset += rs;
    AIODataRead(conn);
#else
    } while (rs) ;
#endif
}

/* Reading buffer */
void
NNSendLocalArticle(Connection *conn)
{
#if USE_AIO
    /* let's start the pump */
    AIODataRead(conn);
#else
    /* blocking loop */
    NNSLA_Loop(conn);
#endif
}

/* Copying article... Return 1 when an error occurred */
int
NNCopyLocalArticle(Connection *conn, int rs)
{
    ServReq *sreq = conn->co_SReq;
    DirectFileAccess *dfa = conn->co_DirectFA;
    int sizofbuf = sizeof(dfa->dfa_Buffer) ;
    int i=0, ptr=0 ;
    char *vs;

    while (i < rs) {
      switch (dfa->dfa_InHdr) {
          case INBODY : /* body, just regular copy */
              /* Fast read, halt on end of buffer/line */
              while ( (dfa->dfa_Buffer[i] != '\n') && (dfa->dfa_Buffer[i] != '\r') && !dfa->dfa_LF && (i+1<rs) ) {
                  i++ ;
              }
              switch (dfa->dfa_Buffer[i]) {
                  case '\n' :
                      if ( dfa->dfa_LF != CR) { /* if I get a CRLF, I can delay the writing */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_LF = CRLF ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      break ;
                  case '.' :
                      if (dfa->dfa_LF == CRLF) { /* we have to dupplicate this '.' */
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          ptr = i ;
                      }
                  default :
                      dfa->dfa_LF = 0 ;
              }
              break ;
          case JMPBDY : /* COM_BODY, del headers */
              /* fast read, halt on end of buffer/line */
              if (dfa->dfa_LF == LF) {
                  switch (dfa->dfa_Buffer[i]) {
                      case '\n' :
                          dfa->dfa_InHdr = INBODY ;
                      case '\t' :
                          ptr = i + 1 ;
                          break ;
                  } 
              }
              while ( (dfa->dfa_Buffer[i]!='\n') && (i+1<rs)) i++ ;
              if (dfa->dfa_Buffer[i]=='\n') 
                  dfa->dfa_LF = LF ;
              else
                  dfa->dfa_LF = 0 ;
              ptr = i + 1 ;
              break ;
          case WRDDEL : /* header, drop current word & copy til the end of line */
              /* Fast read, halt on end of buffer/word */
              while ( (i<rs) && (dfa->dfa_Buffer[i]!=' ') && (dfa->dfa_Buffer[i]!='\t')
                      && (dfa->dfa_Buffer[i]!='\r') && (dfa->dfa_Buffer[i]!='\n') )
                  i++ ;
              ptr = i ;
              if (i==rs) { /* end of buffer... */
                  break ;
              }
              dfa->dfa_LF = 0 ;
          case MAX_HDR_CC : /* stop checking, just copy line */
              dfa->dfa_InHdr = HDRCPY ;
          case HDRCPY : /* header, no more check on this line */
              /* Fast read, halt on end of buffer/line */
              while ( (dfa->dfa_Buffer[i] != '\n') && (dfa->dfa_Buffer[i] != '\r') && !dfa->dfa_LF && (i+1<rs))
                  i++ ;
              switch (dfa->dfa_Buffer[i]) {
                  case '\n' :
                      if (dfa->dfa_LF != CR) { /* no need to copy when having a CRLF */
                          /* copying & adding a CRLF */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_InHdr = 0 ; 
                      dfa->dfa_LF = HDRCPY ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      break ;
                  default :
                      dfa->dfa_LF = 0 ;
                      break ;
              }
              break ;
          case HDRDEL : /* header, drop the rest of the current line */ /* unused */
              /* Fast read, halt on end of buffer/line */
              while ((dfa->dfa_Buffer[i] != '\n') && (i+1<rs)) i++ ;
              if (dfa->dfa_Buffer[i] == '\n') {
                  dfa->dfa_InHdr = 0 ;
                  dfa->dfa_LF = HDRDEL ;
              }
              ptr = i+1 ;
              break ;
          case 0 : /* 1st char of line */
              switch (dfa->dfa_Buffer[i]) {
                  case '\t' :
                  case ' ' : /* header continuation... */
                      if (dfa->dfa_LF < 0) {
                          dfa->dfa_InHdr = dfa->dfa_LF ;
                      } else { /* this should not happen : we should be in HDRCPY or HDRDEL */
                          logit(LOG_ERR, "NNGetLocal2: bad header continuation (%i)", dfa->dfa_LF) ;
                          return BLOCK_ERROR;
                      }
                      break ;
                  case '\r' :
                      /* Check missing headers, end of last buffer or HEAD mode */
                      if ( ((dfa->dfa_Flag&FLAG_NEEDED)==FLAG_NEEDED) || (i+1 == sizofbuf)
                           || (sreq->sr_CConn->co_ArtMode == COM_HEAD)) {
                          /* we "simply" forget this '\r' for laziness reasons */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          ptr = i+1 ;
                          dfa->dfa_LF = 0 ;
                      } else {
                          dfa->dfa_LF = CR ;
                      }
                      /* dfa->dfa_InHdr = 0 ; */
                      break ;
                  case '\n' : /* fin des entetes */
                      if (!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoReadPath 
                           && (vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                           && ((dfa->dfa_Flag&FLAG_PATH) != FLAG_PATH)
                         ) { /* Missing Path */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          ptr = i ;
                          DFADataWrite(sreq, "Path: ", 6) ;
                          DFADataWrite(sreq, vs, strlen(vs)) ;
                          DFADataWrite(sreq, "!not-for-mail\r\n", 15);
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_PATH ;
                      }
                      if (!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate
                           &&(vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                           && ((dfa->dfa_Flag&FLAG_XREF) != FLAG_XREF) 
                         ) { /* Missing Xref */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          ptr = i ;
                          DFADataWrite(sreq, "XRef: ", 6) ;
                          DFADataWrite(sreq, vs, strlen(vs)) ;
                          DFADataWrite(sreq, " no-xref\r\n", 10) ;
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_XREF ;
                      }

                      if (sreq->sr_CConn->co_ArtMode == COM_HEAD) { /* end of header in HEAD mode */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          return BLOCK_END;
                      }
                      if ( dfa->dfa_LF != CR) { /* if I get a CRLF, I can delay the writings */
                          dfa->dfa_Buffer[i] = '\r' ;
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          dfa->dfa_Buffer[i] = '\n' ;
                          ptr = i ;
                      }
                      dfa->dfa_LF = CRLF ;
                      dfa->dfa_InHdr = INBODY ;
                      break ;
                  default :
                      dfa->dfa_LF = 0 ;
                      dfa->dfa_Field[(int)dfa->dfa_InHdr] = dfa->dfa_Buffer[i] ;
                      dfa->dfa_InHdr++ ;
                      break ;
              }
              break ;
          case 0x05 : /* 6th char of line */
              switch (dfa->dfa_Buffer[i]) {
                  case ' ' :
                      if (!sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoXrefHostUpdate
                           && (vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                           && (strncasecmp(dfa->dfa_Field, "Xref:",5) == 0)
                         ) {
                          /* copying & adding a CRLF */
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          ptr = i + 1 ;
                          /* We just replace hostname in Xref, maybe we should
                           * replace the full line with overview content ? */
                          DFADataWrite(sreq, vs, strlen(vs)) ;
                          dfa->dfa_InHdr = WRDDEL ;
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_XREF ;
                      } else if ( !sreq->sr_CConn->co_Auth.dr_VServerDef->vs_NoReadPath 
                                  && (vs=sreq->sr_CConn->co_Auth.dr_VServerDef->vs_ClusterName)
                                  && (strncasecmp(dfa->dfa_Field, "Path:",5) == 0)  
                                ) {  /* it is possible to "double" path if reader & spooler has 
                                      * the same name but it would be painfull to check the first
                                      * path host... */
                          DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr+1) ;
                          ptr = i + 1 ;
                          DFADataWrite(sreq, vs, strlen(vs)) ;
                          DFADataWrite(sreq, "!", 1) ;
                          dfa->dfa_InHdr = HDRCPY ;
                          dfa->dfa_Flag = dfa->dfa_Flag | FLAG_PATH ;
                      } else {
                          dfa->dfa_InHdr = HDRCPY ;
                      }
                      break ;
                  default : /* no more check... */
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
              }
              break ;
          default : /* Else, we are reading firsts chars on headers lines */
              if (dfa->dfa_InHdr<0) { /* ooops */
                  return BLOCK_ERROR;
              }
              switch (dfa->dfa_Buffer[i]) {
                  case ' ' : /* end of "keyword: ", thus go for a HDRCPY */
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
                  case '\n' :
                      if (dfa->dfa_LF != CR) { /* no need to copy when having a CRLF */
                          /* copying & adding a CRLF */
                          if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;
                          DFADataWrite(sreq, "\r\n", 2) ;
                          ptr = i + 1 ;
                      }
                      dfa->dfa_LF = HDRCPY ; /* in case this header continues on the following line */
                      dfa->dfa_InHdr = 0 ;
                      break ;
                  case '\r' :
                      dfa->dfa_LF = CR ;
                      dfa->dfa_InHdr = HDRCPY ;
                      break ;
                  default :
                      dfa->dfa_LF = 0 ;
                      dfa->dfa_Field[(int)dfa->dfa_InHdr] = dfa->dfa_Buffer[i] ;
                      dfa->dfa_InHdr++ ;
                      break ;
              }
              break ;
      }

      i++ ;
    }

    if (i-ptr) DFADataWrite(sreq, dfa->dfa_Buffer+ptr, i-ptr) ;

    return BLOCK_OK;
}

