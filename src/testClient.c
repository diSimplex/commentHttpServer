/*

A very simple client to test the commentHttpServer.

*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TRUE  1
#define FALSE 0
#define IP_ADDRESS "127.0.0.1"
#define BUFFER_SIZE 8196

static struct sockaddr_in serv_addr;

int sentRequest(int port, char *testFileName, char *responseKey) {

  printf("\n") ;

  char testFilePath[BUFFER_SIZE+1];
  memset(testFilePath, 0, BUFFER_SIZE+1) ;
  if (snprintf(testFilePath, BUFFER_SIZE, "testFiles/%s", testFileName) < 0) {
    printf("Could not create testFilePath for %s\n", testFileName)	;
    return FALSE ;
  }

  FILE *testFile = fopen(testFilePath, "r") ;
  if (! testFile) {
    printf("Could not open test file: %s\n", testFilePath ) ;
    return FALSE ;
  }

  char requestBuffer[BUFFER_SIZE+1];
  memset(requestBuffer, 0, BUFFER_SIZE+1) ;
  size_t requestLen = fread(requestBuffer, 1, BUFFER_SIZE, testFile) ;
  if ( requestLen < 1) {
  	printf("Could not read test file: %s\n", testFilePath ) ;
  	return FALSE ;
  }

	int serverFD = socket(AF_INET, SOCK_STREAM, 0 ) ;
	if( serverFD < 0 ) {
  	printf("Could not connect to http://%s:%d\n", IP_ADDRESS, port) ;
	  return FALSE ;
	}

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = inet_addr(IP_ADDRESS);
  serv_addr.sin_port = htons(port);

  if(connect(serverFD, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0 ) {
  	printf("ERROR: Could not connect to http://%s:%d\n", IP_ADDRESS, port) ;
  	return FALSE ;
  }

	write(serverFD, requestBuffer, requestLen ) ;

  char responseBuffer[BUFFER_SIZE+1];

	while( TRUE ) {
    memset(responseBuffer, 0, BUFFER_SIZE+1) ;
	  int bytesRead = read(serverFD, responseBuffer, BUFFER_SIZE ) ;
	  if (bytesRead < 0) {
	    printf("Could not read from server\n") ;
	  	shutdown(serverFD, SHUT_RDWR) ;
    	close(serverFD) ;
	  }
	  if (bytesRead == 0 ) {
	  	break ;
	  }

	  char *needle = strcasestr(responseBuffer, responseKey) ;
	  if ( needle == 0 ) {
	  	printf("WRONG: status\n") ;
	  	char *eol = strchr(responseBuffer, '\r') ;
	  	if (eol) *eol = 0;
	  	eol = strchr(responseBuffer, '\n') ;
	  	if (eol) *eol = 0;
      printf("  [%s]\n", responseBuffer) ;
  	  shutdown(serverFD, SHUT_RDWR) ;
	    close(serverFD) ;
	  	return FALSE ;
	  }
	}

	shutdown(serverFD, SHUT_RDWR) ;
	close(serverFD) ;
	return TRUE ;
}

void sendRequest(int port, char *testFileName, char *responseKey) {
  if (! sentRequest(port, testFileName, responseKey) )
    printf("FAILED: %s\n", testFileName) ;
  else
    printf("SUCCESS: %s\n", testFileName) ;
}

int curledRequest(int port, char *testFileName, char *responseKey) {
  printf("\n") ;

  char testFilePath[BUFFER_SIZE+1];
  memset(testFilePath, 0, BUFFER_SIZE+1) ;
  if (snprintf(testFilePath, BUFFER_SIZE, "testFiles/%s", testFileName) < 0) {
    printf("Could not create testFilePath for %s\n", testFileName)	;
    return FALSE ;
  }

  char cmdBuffer[BUFFER_SIZE+1];
  memset(cmdBuffer, 0, BUFFER_SIZE+1) ;
  if (snprintf(
    cmdBuffer, BUFFER_SIZE,
    "curl -F 'data=@%s' http://127.0.0.1:%d", testFilePath, port
  ) < 0 ) {
  	printf("Could not create cmd for %s\n", testFilePath) ;
  	return FALSE;
  }
  printf("running cmd: [%s]\n", cmdBuffer) ;

  FILE *pFile = popen(cmdBuffer, "r") ;
  if (! pFile ) {
  	printf("Could not open pipe to cmd %s\n", cmdBuffer) ;
  	return FALSE;
  }

  char responseBuffer[BUFFER_SIZE+1];

	while( TRUE ) {
    memset(responseBuffer, 0, BUFFER_SIZE+1) ;
	  int bytesRead = fread(responseBuffer, 1, BUFFER_SIZE, pFile ) ;
	  if (bytesRead < 0) {
	    printf("Could not read from server\n") ;
    	fclose(pFile) ;
	  }
	  if (bytesRead == 0 ) {
	  	break ;
	  }

	  char *needle = strcasestr(responseBuffer, responseKey) ;
	  if ( needle == 0 ) {
	  	printf("WRONG: status\n") ;
	  	char *eol = strchr(responseBuffer, '\r') ;
	  	if (eol) *eol = 0;
	  	eol = strchr(responseBuffer, '\n') ;
	  	if (eol) *eol = 0;
      printf("  [%s]\n", responseBuffer) ;
	    fclose(pFile) ;
	  	return FALSE ;
	  }
	}
	fclose(pFile) ;
	return TRUE ;
}

void curlRequest(int port, char *testFileName, char *responseKey) {
	if (! curledRequest(port, testFileName, responseKey) )
    printf("FAILED: %s\n", testFileName) ;
  else
    printf("SUCCESS: %s\n", testFileName) ;
}

int main(int argc, char **argv) {

  if (argc < 2) {
  	printf("Usage: testClient <port>\n") ;
  	exit(-1) ;
  }

	int port = atoi(argv[1]) ;

	sendRequest(port, "plainAscii", "OK") ;
	curlRequest(port, "plainAscii", "Thank you for your comment") ;
  sendRequest(port, "programData", "too large") ;
  curlRequest(port, "programData", "too large") ;
  sendRequest(port, "shortProgDataA", "Invalid UTF-8") ;
  curlRequest(port, "shortProgDataA", "not valid utf-8") ;
  sendRequest(port, "shortProgDataA-noNulls", "Invalid UTF-8") ;
  curlRequest(port, "shortProgDataA-noNulls", "not valid utf-8") ;
  sendRequest(port, "shortProgDataB", "Invalid UTF-8") ;
  curlRequest(port, "shortProgDataB", "not valid utf-8") ;
  sendRequest(port, "shortProgDataB-noNulls", "Invalid UTF-8") ;
  curlRequest(port, "shortProgDataB-noNulls", "not valid utf-8") ;
  sendRequest(port, "UTF-8-demoA", "OK") ;
  curlRequest(port, "UTF-8-demoA", "Thank you for your comment") ;
  sendRequest(port, "UTF-8-demoB", "OK") ;
  curlRequest(port, "UTF-8-demoB", "Thank you for your comment") ;

}