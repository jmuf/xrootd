/******************************************************************************/
/*                                                                            */
/*                         X r d O u c L i n k . c c                          */
/*                                                                            */
/* (c) 2003 by the Board of Trustees of the Leland Stanford, Jr., University  */
/*                            All Rights Reserved                             */
/*   Produced by Andrew Hanushevsky for Stanford University under contract    */
/*              DE-AC03-76-SFO0515 with the Department of Energy              */
/******************************************************************************/

//          $Id$

const char *XrdOucLinkCVSID = "$Id$";
  
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#ifdef __linux__
#include <sys/poll.h>
#else
#include <poll.h>
#endif

#include "XrdOuc/XrdOucBuffer.hh"
#include "XrdOuc/XrdOucError.hh"
#include "XrdOuc/XrdOucLink.hh"
#include "XrdOuc/XrdOucNetwork.hh"
#include "XrdOuc/XrdOucStream.hh"
 
/******************************************************************************/
/*                 S t a t i c   I n i t i a l i z a t i o n                  */
/******************************************************************************/

XrdOucMutex             XrdOucLink::LinkList;
XrdOucStack<XrdOucLink> XrdOucLink::LinkStack;
int                     XrdOucLink::maxlink = 16;
int                     XrdOucLink::numlink = 0;
  
/******************************************************************************/
/*                                 A l l o c                                  */
/******************************************************************************/
  
XrdOucLink *XrdOucLink::Alloc(XrdOucError *erp, int fd, sockaddr_in *ip,
                              char *host, XrdOucBuffer *bp)
{
XrdOucLink *lp;

// Lock the data area
//
   LinkList.Lock();

// Either return a new buffer or an old one
//
   if (!(lp = LinkStack.Pop())) lp = new XrdOucLink(erp);
      else numlink--;

// Unlock the data area
//
   LinkList.UnLock();

// Establish the address and connection type of this link
//
   memcpy((void *)&(lp->InetAddr),(const void *)ip,sizeof(struct sockaddr_in));
   if (host) lp->Lname = strdup(host);
      else lp->Lname = XrdOucNetwork::getHostName(*ip);
   lp->FD = fd;

// Set the buffer pointer for this link
//
  if (bp)  lp->recvbuff = bp;
     else {lp->Stream = new XrdOucStream(erp);
           lp->Stream->Attach(lp->FD);
          }

// Return the link
//
   return lp;
}
  
/******************************************************************************/
/*                                 C l o s e                                  */
/******************************************************************************/
  
int XrdOucLink::Close(int defer)
{
    IOMutex.Lock();
    if (FD >= 0)
       {if (Stream) Stream->Close();
           else close(FD);
        FD = -1;
       }
    if (Lname) free(Lname);
    if (!defer)
       {if (Stream)   {delete Stream; Stream = 0;}
        if (recvbuff) {recvbuff->Recycle(); recvbuff = 0;}
        if (sendbuff) {sendbuff->Recycle(); sendbuff = 0;}
       }
    IOMutex.UnLock();
    return 0;
}

/******************************************************************************/
/*                               G e t L i n e                                */
/******************************************************************************/
  
char *XrdOucLink::GetLine()
{
     if (Stream) return Stream->GetLine();
     if (recvbuff && recvbuff->dlen) return recvbuff->data;
     return 0;
}

/******************************************************************************/
/*                              G e t T o k e n                               */
/******************************************************************************/
  
char *XrdOucLink::GetToken(char **rest)
{
     if (Stream)   return Stream->GetToken(rest);
     if (recvbuff) return recvbuff->Token(rest);
     return 0;
}
  
char *XrdOucLink::GetToken()
{
     if (Stream)   return Stream->GetToken();
     if (recvbuff) return recvbuff->Token();
     return 0;
}

/******************************************************************************/
/*                             L a s t E r r o r                              */
/******************************************************************************/
  
int XrdOucLink::LastError()
{
    return (Stream ? Stream->LastError() : 0);
}

/******************************************************************************/
/*                               R e c y c l e                                */
/******************************************************************************/

void XrdOucLink::Recycle()
{

// Check if we have enough objects, if so, delete ourselves and return
//
   if (numlink >= maxlink) {delete this; return;}
   Close();

// Add the link to the recycle list
//
   LinkList.Lock();
   LinkStack.Push(&LinkLink);
   numlink++;
   LinkList.UnLock();
   return;
}
 
/******************************************************************************/
/*                                  S e n d                                   */
/******************************************************************************/
  
int XrdOucLink::Send(char *Buff, int Blen, int tmo)
{
   int retc;

   if (!Blen && !(Blen = strlen(Buff))) return 0;
   if ('\n' != Buff[Blen-1])
      {struct iovec iodata[2] = {{Buff, Blen}, {(char *)"\n", 1}};
       return Send((const struct iovec *)iodata, 2, tmo);
      }

   IOMutex.Lock();

   if (tmo >= 0 && !OK2Send(tmo))
      {IOMutex.UnLock(); return -2;}

   if (Stream)
      do {retc = write(FD, Buff, Blen);}
         while (retc < 0 && errno == EINTR);
      else
      do {retc = sendto(FD, (void *)Buff, Blen, 0,
                       (struct sockaddr *)&InetAddr, sizeof(InetAddr));}
         while (retc < 0 && errno == EINTR);
   if (retc < 0) return retErr(errno);
   IOMutex.UnLock();
   return 0;
}

int XrdOucLink::Send(void *Buff, int Blen, int tmo)
{
   int retc;

   IOMutex.Lock();

   if (tmo >= 0 && !OK2Send(tmo))
      {IOMutex.UnLock(); return -2;}

   if (Stream)
      do {retc = write(FD, Buff, Blen);}
         while (retc < 0 && errno == EINTR);
      else
      do {retc = sendto(FD, (void *)Buff, Blen, 0,
                       (struct sockaddr *)&InetAddr, sizeof(InetAddr));}
         while (retc < 0 && errno == EINTR);
   if (retc < 0) return retErr(errno);
   IOMutex.UnLock();
   return 0; 
}
  
int XrdOucLink::Send(char *dest, char *Buff, int Blen, int tmo)
{
   int retc;
   struct sockaddr_in destip;

   if (!Blen && !(Blen = strlen(Buff))) return 0;
   if ('\n' != Buff[Blen-1])
      {struct iovec iodata[2] = {{Buff, Blen}, {(char *)"\n", 1}};
       return Send(dest, (const struct iovec *)iodata, 2, tmo);
      }

   if (!XrdOucNetwork::Host2Dest(dest, destip))
      {eDest->Emsg("Link", (const char *)dest, (char *)"is unreachable");
       return -1;
      }

   if (Stream)
      {eDest->Emsg("Link", "Unable to send msg to", dest, 
                           (char *)"on a stream socket");
       return -1;
      }

   IOMutex.Lock();

   if (tmo >= 0 && !OK2Send(tmo, dest))
      {IOMutex.UnLock(); return -2;}

   do {retc = sendto(FD, (void *)Buff, Blen, 0,
                    (struct sockaddr *)&destip, sizeof(destip));}
       while (retc < 0 && errno == EINTR);

   if (retc < 0) return retErr(errno, dest);
   IOMutex.UnLock();
   return 0;
}

int XrdOucLink::Send(const struct iovec iov[], int iovcnt, int tmo)
{
   int i, dsz, retc;
   char *bp;

   IOMutex.Lock();

   if (tmo >= 0 && !OK2Send(tmo))
      {IOMutex.UnLock(); return -2;}

   if (Stream)
      do {retc = writev(FD, iov, iovcnt);}
         while (retc < 0 && errno == EINTR);
      else
      {if (!sendbuff && !(sendbuff = XrdOucBuffer::Alloc())) return retErr(ENOMEM);
       dsz = sendbuff->BuffSize(); bp = sendbuff->data;
       for (i = 0; i < iovcnt; i++)
           {dsz -= iov[i].iov_len;
            if (dsz < 0) return retErr(EMSGSIZE);
            memcpy((void *)bp,(const void *)iov[i].iov_base,iov[i].iov_len);
            bp += iov[i].iov_len;
           }
       do {retc = sendto(FD, (void *)sendbuff->data, (int)(bp-sendbuff->data), 0,
                       (struct sockaddr *)&InetAddr, sizeof(InetAddr));}
           while (retc < 0 && errno == EINTR);
       }

   if (retc < 0) return retErr(errno);
   IOMutex.UnLock();
   return 0;
}

int XrdOucLink::Send(char *dest, const struct iovec iov[], int iovcnt, int tmo)
{
   int i, dsz, retc;
   char *bp;
   struct sockaddr_in destip;

   if (!XrdOucNetwork::Host2Dest(dest, destip))
      {eDest->Emsg("Link", (const char *)dest, (char *)"is unreachable");
       return -1;
      }

   if (Stream)
      {eDest->Emsg("Link", "Unable to send msg to", dest, 
                   (char *)"on a stream socket");
       return -1;
      }

   IOMutex.Lock();

   if (tmo >= 0 && !OK2Send(tmo, dest))
      {IOMutex.UnLock(); return -2;}

   if (!sendbuff && !(sendbuff = XrdOucBuffer::Alloc())) return retErr(ENOMEM);
   dsz = sendbuff->BuffSize(); bp = sendbuff->data;
   for (i = 0; i < iovcnt; i++)
       {dsz -= iov[i].iov_len;
        if (dsz < 0) return retErr(EMSGSIZE);
        memcpy((void *)bp,(const void *)iov[i].iov_base,iov[i].iov_len);
        bp += iov[i].iov_len;
       }
   do {retc = sendto(FD, (void *)sendbuff->data, (int)(bp-sendbuff->data), 0,
                    (struct sockaddr *)&destip, sizeof(destip));}
      while (retc < 0 && errno == EINTR);

   if (retc < 0) return retErr(errno, dest);
   IOMutex.UnLock();
   return 0;
}

/******************************************************************************/
/*                                  R e c v                                   */
/******************************************************************************/
  
int XrdOucLink::Recv(char *Buff, long Blen)
{
   ssize_t rlen;

// Note that we will read only as much as is queued. Use Recv() with a
// timeout to receive as much data as possible.
//
   IOMutex.Lock();
   do {rlen = read(FD, Buff, Blen);} while(rlen < 0 && errno == EINTR);
   IOMutex.UnLock();

   if (rlen >= 0) return (int)rlen;
   eDest->Emsg("Link", errno, "receiving from", Lname);
   return -1;
}


/******************************************************************************/
/*                                   S e t                                    */
/******************************************************************************/
  
void XrdOucLink::Set(int maxl)
{

// Lock the data area, set max links, unlock and return
//
   LinkList.Lock();
   maxlink = maxl;
   LinkList.UnLock();
   return;
}
 
/******************************************************************************/
/*                               S e t O p t s                                */
/******************************************************************************/
  
void XrdOucLink::SetOpts(int opts)
{

// Set options we care about
//
   if (opts &OUC_LINK_NOBLOCK) fcntl(FD, F_SETFL, O_NONBLOCK);
}
  
/******************************************************************************/
/*                       P r i v a t e   M e t h o d s                        */
/******************************************************************************/
/******************************************************************************/
/*                               O K 2 S e n d                                */
/******************************************************************************/
  
int XrdOucLink::OK2Send(int timeout, char *dest)
{
   struct pollfd polltab = {FD, POLLOUT|POLLWRNORM, 0};
   int retc;

   do {retc = poll(&polltab, 1, timeout);} while(retc < 0 && errno == EINTR);

   if (retc == 0 || !(polltab.revents & (POLLOUT | POLLWRNORM)))
      eDest->Emsg("Link",(dest ? dest : Lname),(char *)"is blocked.");
      else if (retc < 0)
              eDest->Emsg("Link",errno,"polling",(dest ? dest : Lname));
              else return 1;
   return 0;
}
  
/******************************************************************************/
/*                                r e t E r r                                 */
/******************************************************************************/
  
int XrdOucLink::retErr(int ecode, char *dest)
{
   IOMutex.UnLock();
   eDest->Emsg("Link", ecode, "sending to", (dest ? dest : Lname));
   return (EWOULDBLOCK == ecode | EAGAIN == ecode ? -2 : -1);
}
