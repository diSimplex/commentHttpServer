/*! \file

We implement a very simple request logging http server.

Before we even attempt to write the request to disk, we first check that
it is valid UTF-8.

This code has been inspired from: https://github.com/ankushagarwal/nweb

The UTF-8 validation has been inspired from:
https://helloacm.com/how-to-validate-utf-8-encoding-the-simple-utf-8-validation-algorithm/

The signal and zombie handling have been inspired by:

 - https://github.com/TheAssassin/AppImageLauncher/commit/3f901f6419d9ba5bee15bc101d4b9b0adb5e343e
 - https://www.man7.org/linux/man-pages/man7/signal.7.html
 - https://man7.org/linux/man-pages/man2/wait.2.html

 see also:

  - https://github.com/krallin/tini

*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <time.h>

#define BUFFER_SIZE 8096

char *requestTooLarge =
  "HTTP/1.1 413 Request too large \n"
  "Content-Type: text/html\n\n"
  "<html><body>"
  "<h>Your comment is too large</h>\n"
  "<p>Long comments are really papers in their own right. "
  "Please consider publishing your own paper and then providing a "
  "reference to it as a comment.</p>"
  "</body></html>" ;

char *invalidUft8 =
  "HTTP/1.1 415 Invalid UTF-8 \n"
  "Content-Type: text/html\n\n"
  "<html><body>"
  "<h>Your comment is not valid utf-8</h>\n"
  "<p>We do not accept comments which are not valid utf-8</p>"
  "</body></html>" ;

char *couldNotCollectComment =
  "HTTP/1.1 500 Server error \n"
  "Content-Type: text/html\n\n"
  "<html><body>"
  "<h>Sorry... we could not record you comment at the moment</h>"
  "Something went wrong with our server and we could not deal with your "
  "comment. Please try again later."
  "</body></html>" ;

char *thankYou =
  "HTTP/1.1 200 OK \n"
  "Content-Type: text/html\n\n"
  "<html><body>"
  "<h>Thank you for your comment</h>"
  "Thank you for your comment. Our editors will consider your comment to "
  "determine if it conforms to our comment criteria."
  "</body></html>" ;

#define TRUE  1
#define FALSE 0

#define advanceByte {                        \
	if ( bufferEnd <= curByte ) return FALSE ; \
	curByte++ ;                                \
}

#define isOneByte    (( *curByte & 0b10000000 ) == 0b00000000 )
#define isCharByte   (( *curByte & 0b11000000 ) == 0b10000000 )
#define isTwoBytes   (( *curByte & 0b11100000 ) == 0b11000000 )
#define isThreeBytes (( *curByte & 0b11110000 ) == 0b11100000 )
#define isFourBytes  (( *curByte & 0b11111000 ) == 0b11110000 )

////////////////////////////////////////////////////////////////////////
// Manage the children workers...

#define MAX_NUM_WORKERS 20

pid_t   myPid         = 0 ;
size_t  maxNumWorkers = 0 ;
size_t  curNumWorkers = 0 ;
pid_t  *workerPids    = NULL ;

void createWorkerPids(size_t aMaxNumWorkers) {
  myPid         = getpid() ;

	maxNumWorkers = aMaxNumWorkers ;
	workerPids    = calloc(aMaxNumWorkers, sizeof(pid_t)) ;
	memset(workerPids, 0, sizeof(pid_t)*maxNumWorkers) ;
	curNumWorkers = 0 ;

  printf("%d: Created workerPids(%ld)[%ld]<%p>\n", myPid, curNumWorkers, maxNumWorkers, workerPids) ;
}

void clearWorkerPids(void) {
	if (workerPids) free(workerPids) ;

  myPid         = getpid() ;
	maxNumWorkers = 0 ;
	curNumWorkers = 0 ;
	workerPids    = 0 ;

  printf("%d: Cleared workerPids (%ld)[%ld]<%p>\n", myPid, curNumWorkers, maxNumWorkers, workerPids) ;
}

void addToWorkerPids(pid_t aNewWorker) {
  printf("%d: Registering worker %d (%ld)[%ld]<%p>\n", myPid, aNewWorker, curNumWorkers, maxNumWorkers, workerPids) ;
  if (workerPids) {
	  if (curNumWorkers < maxNumWorkers) {
  		workerPids[curNumWorkers] = aNewWorker ;
  		curNumWorkers++ ;
  	}
  }
}

void removeFromWorkerPids(pid_t aWorkerPid) {
  printf("%d: Removing worker %d (%ld)[%ld]<%p>\n", myPid, aWorkerPid, curNumWorkers, maxNumWorkers, workerPids) ;
  if (workerPids) {
	  for (size_t aWorker = 0 ; aWorker < maxNumWorkers; aWorker++){
  		if (workerPids[aWorker] == aWorkerPid) {
  			workerPids[aWorker] = 0 ;
  			break ;
	  	}
  	}
  }
}

size_t numWorkersRemaining(void) {
  printf("%d: numWorkersRemaining (%ld)[%ld]<%p>\n", myPid, curNumWorkers, maxNumWorkers, workerPids);
  size_t numActiveWorkers = 0 ;
  for (size_t aWorker = 0 ; aWorker < curNumWorkers ; aWorker++ ) {
    printf("%d: workerPids[%ld] = [%d]\n", myPid, aWorker, workerPids[aWorker]) ;
  	if (workerPids[aWorker]) numActiveWorkers++ ;
  }
  printf("%d: remaining: numActiveWorkers %ld\n", myPid, numActiveWorkers) ;
  return numActiveWorkers ;
}

int continueHandlingRequests = TRUE ;

void signalHandler(int sigNum) {
  if (workerPids) {
    // parent ...
  	for (size_t aWorker = 0 ; aWorker < maxNumWorkers ; aWorker++) {
	  	if (workerPids[aWorker]) {
		  	kill(workerPids[aWorker], sigNum) ; // ignore all errors...
		  }
	  }
  } else {
    // child ..
    continueHandlingRequests = FALSE ;
  }
}

void installSignalHanders(void) {
  struct sigaction newAction ;
  newAction.sa_handler = signalHandler ;
  sigemptyset(&newAction.sa_mask) ;
  newAction.sa_flags = 0 ;
	sigaction(SIGINT,  &newAction, NULL) ;
	sigaction(SIGHUP,  &newAction, NULL) ;
	sigaction(SIGTERM, &newAction, NULL) ;
}

////////////////////////////////////////////////////////////////////////
// Validate the request to ensure it is valid UTF-8

/*!

  Work through the given buffer checking for valid UTF-8 tuples of bytes.

  See: http://www.unicode.org/reports/tr36/ for UniCode security
  considerations.

*/
int validUft8( char *buffer, int bytesRead) {
  if ( (int)(strlen(buffer)) != bytesRead ) {
    printf("%d: ERROR: incorrect buffer size validating utf-8\n", myPid) ;
    return FALSE ;
  }
  char *bufferEnd = buffer + bytesRead ;
  for (char *curByte = buffer; curByte < bufferEnd ; curByte++) {

  	if ( isOneByte ) continue;

    // the start of a utf-8 character MUST not be a "charByte"
    if ( isCharByte ) return FALSE ;

  	if ( isTwoBytes ) {
  		advanceByte ;
  		if ( ! isCharByte ) return FALSE ;
      continue ;
   	}

   	if ( isThreeBytes ) {
   		advanceByte ;
   		if ( ! isCharByte ) return FALSE ;
   		advanceByte ;
   		if ( ! isCharByte ) return FALSE ;
      continue ;
   	}

    if ( isFourBytes ) {
   		advanceByte ;
 	  	if ( ! isCharByte ) return FALSE ;
 		  advanceByte ;
 		  if ( ! isCharByte ) return FALSE ;
 		  advanceByte ;
 		  if ( ! isCharByte ) return FALSE ;
 		  continue ;
 		}

 		// we MUST be one of the above alternatives!
 		return FALSE ;
  }

	return TRUE ;
}

void clearBuffer(char *buffer, size_t bufferSize) {
	memset(buffer, 0, bufferSize) ;
}

int readRequest(int httpFD, char *buffer, size_t bufferSize) {
  int curBytesRead  = 0 ;
  char *curBuffer   = buffer ;
  int curBufferSize = bufferSize ;

  clearBuffer(buffer, bufferSize) ;

  while (1) {
  	int bytesRead = read(httpFD, curBuffer, curBufferSize) ;
  	curBuffer[bytesRead] = 0 ;
  	if (bytesRead < 0 ) {
  	  // we have failed to read a new chunk...
  		clearBuffer(buffer, bufferSize) ;
  		return bytesRead ;
  	}

    // we update the current sizes...
    //
  	curBytesRead  += bytesRead ;
  	curBufferSize -= bytesRead ;

  	if (curBufferSize <= 0 ) {
  	  // this chunk is TOO big...
  		clearBuffer(buffer, bufferSize) ;
  		return bufferSize ;
  	}

    // We ONLY proceed IF we have valid UTF-8!
    //
    if ( ! validUft8(buffer, curBytesRead) ) {
    	return -2 ;
    }

 	  char *needleA = strcasestr(curBuffer, "Expect:") ;
	  if ( needleA == 0 ) {
	  	// No expect header found... return result as is...
 	  	return curBytesRead ;
 	  }
 	  char *needleB = strcasestr(needleA, "100-continue") ;
 	  if ( needleB == 0 ) {
 	  	// No expect header found... return result as is...
 	  	return curBytesRead ;
  	}

  	// An expect header has been found... so read another chunk...
  	//
    // we update the current pointers and try again...
    //
  	curBuffer     += bytesRead ;
  }
}

void sendResponse(int httpFD, char *response) {
  (void) write(httpFD, response, strlen(response) ) ;
  shutdown(httpFD, SHUT_RDWR) ;
  close(httpFD) ;
}

void runChildOnPort(int port, char* commentDir) {

	printf("%d: listening on port: %d\n", myPid, port) ;

  int listeningFD = socket( AF_INET, SOCK_STREAM, 0 ) ;
  if( listeningFD < 0 ) {
    printf("%d: ERROR: could not open listening socket\n", myPid) ;
    exit(-1) ;
  }

  static struct sockaddr_in cli_addr;
  static struct sockaddr_in serv_addr;

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  if( bind( listeningFD, (struct sockaddr *)&serv_addr,sizeof(serv_addr) ) < 0 ) {
    printf("%d: ERROR: could not bind to socket\n", myPid) ;
    exit(-1) ;
  }
  if( listen( listeningFD, 64 ) < 0 ) {
    printf("%d: ERROR: could not listen to bound socket\n", myPid) ;
    exit(-1) ;
  }
  for ( size_t requestNum = 1 ; continueHandlingRequests ; requestNum++ ) {
    printf("%d: \n", myPid) ;
    socklen_t length = sizeof(cli_addr);
    int httpFD = accept( listeningFD, (struct sockaddr *)&cli_addr, &length) ;
    if ( httpFD < 0 ) {
    	printf("%d: ERROR: could not accept new connection for request: %ld\n", myPid, requestNum) ;
    	continue ;
    }

    static char buffer[BUFFER_SIZE+1] ;
    clearBuffer(buffer, BUFFER_SIZE+1) ;

    clock_t begin = clock();
    int bytesRead = readRequest( httpFD, buffer, BUFFER_SIZE );
    if ( bytesRead == -2 ) {
    	printf("%d: ERROR: invalid UTF-8 while reading request %ld\n", myPid, requestNum);
    	sendResponse(httpFD, invalidUft8) ;
    	continue;
    }
    if ( bytesRead < 1 ) {
    	printf("%d: ERROR: Could not read request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    clock_t endRead = clock();

    if ( BUFFER_SIZE <= bytesRead ) {
    	printf("%d: ERROR: request too large: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, requestTooLarge) ;
    	continue ;
    }

    if ( ! validUft8(buffer, bytesRead) ) {
    	printf("%d: ERROR: invalid utf8 for request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, invalidUft8) ;
    	continue ;
    }
    clock_t endValid = clock();

    char asciiTime[210];
    memset(asciiTime, 0, 210) ;
    time_t timeNow = time(0) ;
    struct tm *timeNowStruct = localtime(&timeNow) ;
    size_t timeSize = strftime(asciiTime, 200, "%Y-%m-%d_%H-%M-%S", timeNowStruct) ;
    if ( timeSize == 0 ) {
    	printf("%d: ERROR: Could not construct asciiTime for request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    char commentPath[BUFFER_SIZE];
    memset(commentPath, 0, BUFFER_SIZE) ;
    size_t commentPathSize = sprintf(
      commentPath, "%s/%s_%d.comment", commentDir, asciiTime, port
     ) ;
    if ( commentPathSize < 1 ) {
    	printf("%d: ERROR: Could not construct commentPath for request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    FILE *commentFile = fopen(commentPath, "w") ;
    if ( !commentFile ) {
    	printf("%d: ERROR: could not open commentFile for request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }

    int bytesWritten = fwrite( buffer, 1, bytesRead, commentFile ) ;
    if ( bytesWritten != bytesRead ) {
    	printf("%d: ERROR: could not write commentFile for request: %ld\n", myPid, requestNum) ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }

    fclose(commentFile) ;
    printf("%d: SUCCESS: captured comment: [%s] for request: %ld\n", myPid, commentPath, requestNum) ;
  	sendResponse(httpFD, thankYou) ;
    clock_t endWrite = clock();

    double readTime  = (double)( endRead  - begin    ) / CLOCKS_PER_SEC ;
    double validTime = (double)( endValid - endRead  ) / CLOCKS_PER_SEC ;
    double writeTime = (double)( endWrite - endValid ) / CLOCKS_PER_SEC ;
    double totalTime = (double)( endWrite - begin    ) / CLOCKS_PER_SEC ;

    printf("%d:%ld:  readTime: %f\n", myPid, requestNum, readTime) ;
    printf("%d:%ld: validTime: %f\n", myPid, requestNum, validTime) ;
    printf("%d:%ld: writeTime: %f\n", myPid, requestNum, writeTime) ;
    printf("%d:%ld: totalTime: %f\n", myPid, requestNum, totalTime) ;
  }
}

int main(int argc, char **argv) {
  if (argc < 4) {
  	printf("Usage: commentHttpServer <commentDir> <firstPort> <numberWorkers>\n") ;
  	exit(-1) ;
  }


  char *commentDir   = argv[1] ;
  int  firstPort     = atoi(argv[2]) ;
  int  numberWorkers = atoi(argv[3]) ;

  if ((numberWorkers < 1) || (MAX_NUM_WORKERS < numberWorkers)) {
  	printf("The number of workers MUST be between 1 and %d\n", MAX_NUM_WORKERS) ;
  	exit(-1);
  }
  createWorkerPids(numberWorkers) ;
  installSignalHanders() ;

  printf("\n") ;
	printf("Started loggingHttpServer\n") ;
	printf("comment directory: [%s]\n", commentDir) ;
  printf("       first port: %d\n", firstPort) ;
  printf("number of workers: %d\n", numberWorkers) ;

  for (int workerNum = 0; workerNum < numberWorkers; workerNum++) {
  	pid_t workerPid = fork() ;
  	int port = firstPort + workerNum ;
  	if (workerPid > 0 ) {
  		// Parent process...
  	  printf("%d: forked a new worker: %d\n", myPid, workerPid) ;
  		addToWorkerPids(workerPid) ;
  		continue ;
  	}	else if (workerPid == 0 ) {
  		// Child process...
  		myPid = getpid() ;
  		printf("%d: Starting child\n", myPid) ;
  		clearWorkerPids() ;
      runChildOnPort(port, commentDir) ;
      printf("%d: Finished child\n", myPid) ;
      return 0 ;
  	} else {
  		// error!
  		printf("%d: Could not fork worker: %d\n", myPid, workerNum ) ;
  	}
  }

  printf("%d: About to wait on workers %ld\n", myPid, numWorkersRemaining()) ;
  while(0 < numWorkersRemaining()) {
    printf("%d: waiting on children: %ld\n", myPid, numWorkersRemaining()) ;
  	pid_t deadChild = wait(NULL) ;
  	if (0 < deadChild) {
  	  removeFromWorkerPids(deadChild) ;
  	  continue ;
  	}
  	if ( errno == ECHILD ) {
  	  // no more children to wait on
  	  break ;
  	}
    // ignore error and try again
  }

  printf("%d: Done!\n", myPid) ;
}
