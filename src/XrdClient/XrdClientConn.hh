//////////////////////////////////////////////////////////////////////////
//                                                                      //
// XrdClientConn                                                        //
//                                                                      //
// Author: Fabrizio Furano (INFN Padova, 2004)                          //
// Adapted from TXNetFile (root.cern.ch) originally done by             //
//  Alvise Dorigo, Fabrizio Furano                                      //
//          INFN Padova, 2003                                           //
//                                                                      //
// High level handler of connections to xrootd.                         //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

//       $Id$

#ifndef XRD_CONN_H
#define XRD_CONN_H


#include "XrdClient/XrdClientConst.hh"

#include "time.h"
#include "XrdClient/XrdClientConnMgr.hh"
#include "XrdClient/XrdClientMessage.hh"
#include "XrdClient/XrdClientUrlInfo.hh"
#include "XrdClient/XrdClientReadCache.hh"
#include "XrdOuc/XrdOucHash.hh"

#define ConnectionManager XrdClientConn::GetConnectionMgr()

class XrdClientAbs;
class XrdSecProtocol;

class XrdClientConn {

public:
   enum ServerType {
      kSTError      = -1,  // Some error occurred: server type undetermined 
      kSTNone       = 0,   // Remote server type un-recognized
      kSTRootd      = 1,   // Remote server type: old rootd server
      kSTBaseXrootd = 2,   // Remote server type: xrootd dynamic load balancer
      kSTDataXrootd = 3    // Remote server type: xrootd data server
   }; 
   enum ESrvErrorHandlerRetval {
      kSEHRReturnMsgToCaller   = 0,
      kSEHRBreakLoop           = 1,
      kSEHRContinue            = 2,
      kSEHRReturnNoMsgToCaller = 3,
      kSEHRRedirLimitReached   = 4
   };
   enum EThreeStateReadHandler {
      kTSRHReturnMex     = 0,
      kTSRHReturnNullMex = 1,
      kTSRHContinue      = 2
   };

   int                        fLastDataBytesRecv;
   int                        fLastDataBytesSent;
   XErrorCode                 fOpenError;	

  
   XrdClientConn();
   ~XrdClientConn();

   inline bool                CacheWillFit(long long bytes) {
      if (!fMainReadCache)
	 return FALSE;
      return fMainReadCache->WillFit(bytes);
   }

   bool                       CheckHostDomain(XrdClientString hostToCheck,
					      XrdClientString allow, 
					      XrdClientString deny);
   short                      Connect(XrdClientUrlInfo Host2Conn,
				      XrdClientAbsUnsolMsgHandler *unsolhandler);
   void                       Disconnect(bool ForcePhysicalDisc);
   bool                       GetAccessToSrv();
   XrdClientString            GetClientHostDomain() { return fClientHostDomain; }

   bool                       GetDataFromCache(const void *buffer,
					       long long begin_offs,
					       long long end_offs,
					       bool PerfCalc,
                                               long long &lasttakenbyte );
   bool                       SubmitDataToCache(XrdClientMessage *xmsg,
					       long long begin_offs,
					       long long end_offs);

   int                        GetLogConnID() const { return fLogConnID; }
   kXR_unt16                  GetStreamID() const { return fPrimaryStreamid; }

   inline XrdClientUrlInfo    *GetLBSUrl() { return fLBSUrl; }
   inline XrdClientUrlInfo    GetCurrentUrl() { return fUrl; }

   XErrorCode                 GetOpenError() const { return fOpenError; }
   XReqErrorType              GoToAnotherServer(XrdClientUrlInfo newdest);
   bool                       IsConnected() const { return fConnected; }

   struct ServerResponseHeader
                              LastServerResp;

   struct ServerResponseBody_Error
                              LastServerError;

   bool                       SendGenCommand(ClientRequest *req, 
					     const void *reqMoreData,       
					     void **answMoreDataAllocated,
					     void *answMoreData, bool HasToAlloc,
					     char *CmdName);

   ServerType                 GetServerType() const { return fServerType; }
   int                        GetOpenSockFD() const { return fOpenSockFD; }

   void                       SetClientHostDomain(const char *src) { fClientHostDomain = src; }
   void                       SetConnected(bool conn) { fConnected = conn; }

   void                       SetOpenError(XErrorCode err) { fOpenError = err; }
   void                       SetRedirHandler(XrdClientAbs *rh) { fRedirHandler = rh; }

   void                       SetRequestedDestHost(char *newh, kXR_int32 port) {
      fREQUrl = fUrl;
      fREQUrl.Host = newh;
      fREQUrl.Port = port;
      fREQUrl.SetAddrFromHost();
   }

   // Puts this instance in pause state for wsec seconds.
   // A value <= 0 revokes immediately the pause state
   void                       SetREQPauseState(kXR_int32 wsec) {
      // Lock mutex
      fREQWait->Lock();

      if (wsec > 0)
	 fREQWaitTimeLimit = time(0) + wsec;
      else {
	 fREQWaitTimeLimit = 0;
	 fREQWait->Broadcast();
      }

      // UnLock mutex
      fREQWait->UnLock();
   }

   // Puts this instance in connect-pause state for wsec seconds.
   // Any future connection attempt will not happen before wsec
   //  and the first one will be towards the given host
   void                       SetREQDelayedConnectState(kXR_int32 wsec) {
      // Lock mutex
      fREQConnectWait->Lock();

      if (wsec > 0)
	 fREQConnectWaitTimeLimit = time(0) + wsec;
      else {
	 fREQConnectWaitTimeLimit = 0;
	 fREQConnectWait->Broadcast();
      }

      // UnLock mutex
      fREQConnectWait->UnLock();
   }

   void                       SetServerType(ServerType type) { fServerType = type; }
   void                       SetSID(kXR_char *sid);
   inline void                SetUrl(XrdClientUrlInfo thisUrl) { fUrl = thisUrl; }


   // Sends the request to the server, through logconn with ID LogConnID
   // The request is sent with a streamid 'child' of the current one, then marked as pending
   // Its answer will be caught asynchronously
   XReqErrorType              WriteToServer_Async(ClientRequest *req, 
						  const void* reqMoreData);
   static XrdClientConnectionMgr *GetConnectionMgr()
                              { return fgConnectionMgr;} //Instance of the conn manager

private:
   // The handler which first tried to connect somewhere
   XrdClientAbsUnsolMsgHandler *fUnsolMsgHandler;

   XrdClientString            fClientHostDomain; // Save the client's domain name
   bool                       fConnected;
   short                      fGlobalRedirCnt;    // Number of redirections
   time_t                     fGlobalRedirLastUpdateTimestamp; // Timestamp of last redirection

   XrdClientUrlInfo           *fLBSUrl;            // Needed to save the load balancer url

   int                        fLogConnID;        // Logical connection ID used
   kXR_unt16                  fPrimaryStreamid;  // Streamid used for normal communication
                                                 // NB it's a copy of the one contained in
                                                 // the logconn

   short                      fMaxGlobalRedirCnt;
   XrdClientReadCache         *fMainReadCache;

   XrdClientAbs               *fRedirHandler;      // Pointer to a class inheriting from
                                                   // XrdClientAbs providing methods
                                                   // to handle a redir at higher level

   XrdClientString            fRedirInternalToken; // Token returned by the server when
                                                   // redirecting

   XrdClientUrlInfo           fREQUrl;             // For explicitly requested redirs
   time_t                     fREQWaitTimeLimit;   // For explicitly requested pause state
   XrdOucCondVar              *fREQWait;           // For explicitly requested pause state
   time_t                     fREQConnectWaitTimeLimit;   // For explicitly requested delayed reconnect
   XrdOucCondVar              *fREQConnectWait;           // For explicitly requested delayed reconnect

   long                       fServerProto;        // The server protocol
   ServerType                 fServerType;         // Server type as returned by doHandShake() 
                                                   // (see enum ServerType)

   // To keep info about an open session
   struct                     SessionIDInfo {
     char id[16];
   };

   static XrdOucHash<SessionIDInfo>
                              fSessionIDRepo;      // The reposiry of session IDs, shared.
                                                   // Association between
                                                   // <hostname>:<port> and a SessionIDInfo struct
                  

   char                       fSessionID[16];          // The ID of this session got from the login

   XrdClientUrlInfo           fUrl;                // The current URL

   int                        fOpenSockFD;         // Descriptor of the underlying socket
   static XrdClientConnectionMgr *fgConnectionMgr; //Instance of the Connection Manager

   bool                       CheckErrorStatus(XrdClientMessage *, short &, char *);
   void                       CheckPort(int &port);
   void                       CheckREQPauseState();
   void                       CheckREQConnectWaitState();
   bool                       CheckResp(struct ServerResponseHeader *resp, const char *method);
   XrdClientMessage           *ClientServerCmd(ClientRequest *req,
					       const void *reqMoreData,
					       void **answMoreDataAllocated,
					       void *answMoreData,
					       bool HasToAlloc);
   XrdSecProtocol            *DoAuthentication(char *plist, int plsiz);
   ServerType                 DoHandShake(short log);
   bool                       DoLogin();

   

   XrdClientString            GetDomainToMatch(XrdClientString hostname);

   ESrvErrorHandlerRetval     HandleServerError(XReqErrorType &, XrdClientMessage *,
						ClientRequest *);
   bool                       MatchStreamid(struct ServerResponseHeader *ServerResponse);

   // Sends a close request, without waiting for an answer
   // useful (?) to be sent just before closing a badly working stream
   bool                       PanicClose();

   XrdClientString            ParseDomainFromHostname(XrdClientString hostname);

   XrdClientMessage           *ReadPartialAnswer(XReqErrorType &, size_t &, 
						 ClientRequest *, bool, void**,
						 EThreeStateReadHandler &);

   void                       ClearSessionID();

   XReqErrorType              WriteToServer(ClientRequest *req, 
					    const void* reqMoreData,
					    short LogConnID);


};



#endif
