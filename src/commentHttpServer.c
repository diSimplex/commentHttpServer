/*

We implement a very simple request logging http server.

Before we even attempt to write the request to disk, we first check that
it is valid UTF-8.

*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
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

int validUft8( char *buffer, int bytesRead) {
  if ( (int)(strlen(buffer)) != bytesRead ) {
    printf("ERROR: incorrect buffer size validating utf-8\n") ;
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

void clearRequestBuffer(char *buffer, size_t bufferSize) {
	memset(buffer, 0, bufferSize) ;
}

int readRequest(int httpFD, char *buffer, size_t bufferSize) {
  int curBytesRead  = 0 ;
  char *curBuffer   = buffer ;
  int curBufferSize = bufferSize ;

  clearRequestBuffer(buffer, bufferSize) ;

  while (1) {
  	int bytesRead = read(httpFD, curBuffer, curBufferSize) ;
  	curBuffer[bytesRead] = 0 ;
  	if (bytesRead < 0 ) {
  	  // we have failed to read a new chunk...
  		clearRequestBuffer(buffer, bufferSize) ;
  		return bytesRead ;
  	}

    // we update the current sizes...
    //
  	curBytesRead  += bytesRead ;
  	curBufferSize -= bytesRead ;

  	if (curBufferSize <= 0 ) {
  	  // this chunk is TOO big...
  		clearRequestBuffer(buffer, bufferSize) ;
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

int main(int argc, char **argv) {
  if (argc < 3) {
  	printf("Usage: commentHttpServer <commentDir> <port>\n") ;
  	exit(-1) ;
  }
  char *commentDir = argv[1] ;
  int  port        = atoi(argv[2]) ;

  printf("\n") ;
	printf("Started loggingHttpServer\n") ;
	printf("comment directory: [%s]\n", commentDir) ;
	printf("listening on port: %d\n", port) ;

  int listeningFD = socket( AF_INET, SOCK_STREAM, 0 ) ;
  if( listeningFD < 0 ) {
    printf("ERROR: could not open listening socket") ;
    exit(-1) ;
  }

  static struct sockaddr_in cli_addr;
  static struct sockaddr_in serv_addr;

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  if( bind( listeningFD, (struct sockaddr *)&serv_addr,sizeof(serv_addr) ) < 0 ) {
    printf("ERROR: could not bind to socket\n") ;
    exit(-1) ;
  }
  if( listen( listeningFD, 64 ) < 0 ) {
    printf("ERROR: could not listen to bound socket\n") ;
    exit(-1) ;
  }
  for ( size_t requestNum = 1 ; ; requestNum++ ) {
    printf("\n") ;
    socklen_t length = sizeof(cli_addr);
    int httpFD = accept( listeningFD, (struct sockaddr *)&cli_addr, &length) ;
    if ( httpFD < 0 ) {
    	printf("ERROR: could not accept new connection\n") ;
    	continue ;
    }

    static char buffer[BUFFER_SIZE+1] ;
    clearRequestBuffer(buffer, BUFFER_SIZE+1) ;

    clock_t begin = clock();
    int bytesRead = readRequest( httpFD, buffer, BUFFER_SIZE );
    if ( bytesRead == -2 ) {
    	printf("ERROR: invalid UTF-8 while reading request\n");
    	sendResponse(httpFD, invalidUft8) ;
    	continue;
    }
    if ( bytesRead < 1 ) {
    	printf("ERROR: Could not read request\n") ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    clock_t endRead = clock();

    if ( BUFFER_SIZE <= bytesRead ) {
    	printf("ERROR: request too large\n") ;
    	sendResponse(httpFD, requestTooLarge) ;
    	continue ;
    }

    if ( ! validUft8(buffer, bytesRead) ) {
    	printf("ERROR: invalid utf8\n") ;
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
    	printf("ERROR: Could not construct asciiTime\n") ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    char commentPath[BUFFER_SIZE];
    memset(commentPath, 0, BUFFER_SIZE) ;
    size_t commentPathSize = sprintf(
      commentPath, "%s/%s_%d.comment", commentDir, asciiTime, port
     ) ;
    if ( commentPathSize < 1 ) {
    	printf("ERROR: Could not construct commentPath\n") ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }
    FILE *commentFile = fopen(commentPath, "w") ;
    if ( !commentFile ) {
    	printf("ERROR: could not open commentFile\n") ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }

    int bytesWritten = fwrite( buffer, 1, bytesRead, commentFile ) ;
    if ( bytesWritten != bytesRead ) {
    	printf("ERROR: could not write commentFile\n") ;
    	sendResponse(httpFD, couldNotCollectComment) ;
    	continue ;
    }

    fclose(commentFile) ;
    printf("SUCCESS: captured comment: %s\n", commentPath) ;
  	sendResponse(httpFD, thankYou) ;
    clock_t endWrite = clock();

    double readTime  = (double)( endRead  - begin    ) / CLOCKS_PER_SEC ;
    double validTime = (double)( endValid - endRead  ) / CLOCKS_PER_SEC ;
    double writeTime = (double)( endWrite - endValid ) / CLOCKS_PER_SEC ;
    double totalTime = (double)( endWrite - begin    ) / CLOCKS_PER_SEC ;

    printf(" readTime: %f\n", readTime) ;
    printf("validTime: %f\n", validTime) ;
    printf("writeTime: %f\n", writeTime) ;
    printf("totalTime: %f\n", totalTime) ;
  }
}
