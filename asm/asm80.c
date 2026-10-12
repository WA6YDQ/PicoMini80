/* 
asm80 - 8080 assembler 

This is an 8080 Assembler written under Linux
This is NOT a macro assembler

(C) 10/2026 Kurt Theis <theis.kurt@gmail.com>

TODO allow labels in DS

*/

#define _GNU_SOURCE		/* allows strcasestr instruction in string.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>

#define LINESIZE 132		// max line length
#define LABELSIZE 40		// max length of a label

/* Globals */


// labels: max of 1000, longest length is 40 chars
int labelcount;
char labelname[1000][LABELSIZE];		// hold list of labels
uint16_t labeladdress[1000];	// hold address of label


// Intel Opcode Names from MCS-80 (C) 1976
char opcodes8080[256][10] = { \
"nop", "lxi b", "stax b", "inx b", "inr b", "dcr b", \
"mvi b", "rlc", "__8", "dad b", "ldax b", "dcx b", \
"inr c", "dcr c", "mvi c", "rrc", "_10", "lxi d", \
"stax d", "inx d", "inr d", "dcr d", "mvi d", "ral", \
"_18", "dad d", "ldax d", "dcx d", "inr e", "dcr e", \
"mvi e", "rar", "_20", "lxi h", "shld", "inx h", \
"inr h", "dcr h", "mvi h", "daa", "_28", "dad h", \
"lhld", "dcx h", "inr l", "dcr l", "mvi l", "cma", \
"_30", "lxi sp", "sta", "inx sp", "inr m", "dcr m", \
"mvi m", "stc", "_38", "dad sp", "lda", "dcx sp", \
"inr a", "dcr a", "mvi a", "cmc", "mov b,b", "mov b,c", \
"mov b,d", "mov b,e", "mov b,h", "mov b,l", "mov b,m", "mov b,a", \
"mov c,b", "mov c,c", "mov c,d", "mov c,e", "mov c,h", "mov c,l", \
"mov c,m", "mov c,a", "mov d,b", "mov d,c", "mov d,d", "mov d,e", \
"mov d,h", "mov d,l", "mov d,m", "mov d,a", "mov e,b", "mov e,c", \
"mov e,d", "mov e,e", "mov e,h", "mov e,l", "mov e,m", "mov e,a", \
"mov h,b", "mov h,c", "mov h,d", "mov h,e", "mov h,h", "mov h,l", \
"mov h,m", "mov h,a", "mov l,b", "mov l,c", "mov l,d", "mov l,e", \
"mov l,h", "mov l,l", "mov l,m", "mov l,a", "mov m,b", "mov m,c", \
"mov m,d", "mov m,e", "mov m,h", "mov m,l", "hlt", "mov m,a", \
"mov a,b", "mov a,c", "mov a,d", "mov a,e", "mov a,h", "mov a,l", \
"mov a,m", "mov a,a", "add b", "add c", "add d", "add e", \
"add h", "add l", "add m", "add a", "adc b", "adc c", \
"adc d", "adc e", "adc h", "adc l", "adc m", "adc a", \
"sub b", "sub c", "sub d", "sub e", "sub h", "sub l", \
"sub m", "sub a", "sbb b", "sbb c", "sbb d", "sbb e", \
"sbb h", "sbb l", "sbb m", "sbb a", "ana b", "ana c", \
"ana d", "ana e", "ana h", "ana l", "ana m", "ana a", \
"xra b", "xra c", "xra d", "xra e", "xra h", "xra l", \
"xra m", "xra a", "ora b", "ora c", "ora d", "ora e", \
"ora h", "ora l", "ora m", "ora a", "cmp b", "cmp c", \
"cmp d", "cmp e", "cmp h", "cmp l", "cmp m", "cmp a", \
"rnz", "pop b", "jnz", "jmp", "cnz", "push b", \
"adi", "rst 0", "rz", "ret", "jz", "_cb", \
"cz", "call", "aci", "rst 1", "rnc", "pop d", \
"jnc", "out", "cnc", "push d", "sui", "rst 2", \
"rc", "_d9", "jc", "in", "cc", "_dd", \
"sbi", "rst 3", "rpo", "pop h", "jpo", "xthl", \
"cpo", "push h", "ani", "rst 4", "rpe", "pchl", \
"jpe", "xchg", "cpe", "_ed", "xri", "rst 5", \
"rp", "pop psw", "jp", "di", "cp", "push psw", \
"ori", "rst 6", "rm", "sphl", "jm", "ei", \
"cm", "_fd", "cpi", "rst 7" };

// size of 8080 opcodes
int opcode_bytes8080[256] = { \
1,3,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,3,1,1,1,1,2, \
1,1,1,1,1,1,1,2,1,1,3,3,1,1,1,2,1,1,1,3,1,1,1,2, \
1,1,3,3,1,1,1,2,1,1,1,3,1,1,1,2,1,1,1,1,1,1,1,1, \
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, \
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, \
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, \
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, \
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, \
1,1,1,3,3,3,1,2,1,1,1,3,1,3,3,2,1,1,1,3,2,3,1,2, \
1,1,1,3,2,3,1,2,1,1,1,3,1,3,1,2,1,1,1,3,1,3,1,2, \
1,1,1,3,1,3,1,2,1,1,1,3,1,3,1,2,1 };


int isReserved(char *token){	/* return 1 if match to reserved work, 0 if not */
int n;
const char reserved[21][8] = {"db",".db","DB",".DB", \
    		   "ds",".ds","DS",".DS", \
	           "dw",".dw","DW",".DW", \
     		   "org",".org","ORG",".ORG", \
		   "equ",".equ","EQU",".EQU"};

	for (n=0; n<255; n++){
		if (strcmp(token,opcodes8080[n])==0)	// test all opcodes
			return 1;
	}

	for (n=0; n<20; n++) {
		if (strcmp(token,reserved[n])==0)	// other reserved words
		    	return 1;
	}

	return 0;	/* everything else is OK */
}


int isOpcode(char *token){	/* check if opcode - return byte size or -1 */

	int n;
	// first change to lowercase token
	char teststr[LABELSIZE] = {'\0'};
	for (n=0; n<strlen(token); n++) 
		teststr[n] = tolower(token[n]);
	for (n=0; n<256; n++)
		if (strcmp(teststr,opcodes8080[n])==0)
			return opcode_bytes8080[n];
	return -1;	/* token not an opcode */
}

int hex2dec(char h){            /* convert single hex digit to decimal */
int byte;
        if(isalpha(h)){     /* 1st digit */
                byte = toupper(h);
                byte = byte - 55;
        }
        if (isdigit(h)){
                byte = h - 48;
        }
        return(byte);
}


int getAddr(char *addr){	/* get address of passed variable - numbers can be 1234, 1234h, 0x1234 */
int n;
char value[10];

	if (strlen(addr)==0)	/* discard bad token (last token will be null in pass 2) */
		return -1;


	/* for 0xnn type of hex numbers */
	if (addr[1] == 'x') {
		n=strtol(addr,NULL,0);
		return n;
	}

	/* for 10h, 1234h type of hex numbers */
	if ((addr[strlen(addr)-1] == 'h') || (addr[strlen(addr)-1] == 'H')){ /* hex number */
		if (strlen(addr)==2) {	/* ex 8h */
			n = hex2dec(addr[0]);
			if (n < 0 || n > 255) return -1;
			return n;
		}
		if (strlen(addr)==3){	/* ex 45H */
			n = hex2dec(addr[1]);
			n += hex2dec(addr[0]) * 16;
			if (n < 0 || n > 255)	/* error checking */
				return -1;
			return n;
		}
		if (strlen(addr)==4){	/* ex 100H */
			n = hex2dec(addr[2]);
			n += hex2dec(addr[1]) * 16;
			n += hex2dec(addr[0]) * 256;
			if (n < 0 || n > 4095)	/* error checking */
				return -1;
			return n;
		}
		if (strlen(addr)==5){	/* ex 2345H */
			n = hex2dec(addr[3]);
			n += hex2dec(addr[2]) * 16;
			n += hex2dec(addr[1]) * 256;
			n += hex2dec(addr[0]) * 4096;
			if (n < 0 || n > 65535)	/* error checking */
				return -1;
			return n;
		}
		return -1;	/* wrong size */
	}
	if ((strlen(addr)>1) && (atoi(addr)==0))	/* bad number */
		return -1;

	if ((strlen(addr)==1) && (isdigit(addr[0])==0))	/* not a number */
		return -1;

	/* address is decimal */
	n = atoi(addr);
	if ((n < 0) || (n > 65535))		/* error checking */
		return -1;
	return n;
}

int isLabel(char *token) {
	int n;
	//printf("isLabel(%s) ",token);
	for (n=0; n<=labelcount; n++){
		if (!strcmp(labelname[n], token)) 
			return labeladdress[n];
	}
	// not an assigned label
	return -1;
}

int getOpCode(char *token){	/* find opcode, return value */
	int n;
	// first change to lowercase token
	char teststr[LABELSIZE] = {'\0'};
	for (n=0; n<strlen(token); n++) 
		teststr[n] = tolower(token[n]);
	for (n=0; n<256; n++)
		if (strcmp(teststr,opcodes8080[n])==0)
			return n;
	return -1;	/* token not an opcode */
}





/* ************** MAIN() ********************* */
int main(int argv, char **argc) {

	int linenumber = 0;
	int address = 0, maxaddress = 0, lowestaddress = 65536;
	int res = 0, opcode_size = 0;
	uint8_t memory[65536] = {'\0'};
	
	char *p = NULL;
	
	char line[LINESIZE];
	char str1[LABELSIZE], str2[LABELSIZE], str3[LABELSIZE], str4[LABELSIZE], str5[LABELSIZE], str6[LABELSIZE];
	
	// if argv, open a file
	if (argv < 2) {
		printf("Usage: %s [file.asm]\n",argc[0]);
		return -1;
	}
	
	FILE *infile = fopen(argc[1],"r");
	if (infile == NULL) {
		printf("File %s not found\n",argc[1]);
		return -1;
	}
	
	// Create a .com filename
	char comfilename[LABELSIZE] = {'\0'};
	strcpy(comfilename,argc[1]);
	p = strchr(comfilename,'.');	// remove extension
	if (p) *p = '\0';
	strcat(comfilename,".com");
	printf("com file is %s\n",comfilename);
	FILE *comfile;
	
	// Create a .hex filename
	char hexfilename[LABELSIZE] = {'\0'};
	strcpy(hexfilename,argc[1]);
	p = strchr(hexfilename,'.');	// remove extension
	if (p) *p = '\0';
	strcat(hexfilename,".hex");
	printf("hex file is %s\n",hexfilename);
	FILE *hexfile;
	
	// Create a .lst filename
	char lstfilename[LABELSIZE] = {'\0'};
	strcpy(lstfilename,argc[1]);
	p = strchr(lstfilename,'.');	// remove extension
	if (p) *p = '\0';
	strcat(lstfilename,".lst");
	printf("list file is %s\n",lstfilename);
	FILE *lstfile;
	lstfile = fopen(lstfilename,"w");
	if (lstfile == NULL) {
		printf("Error Creating listfile %s\n",lstfilename);
		fclose(infile);
		return 1;
	}
	
	fprintf(lstfile,"Assembler listing of file %s\n",argc[1]);
	
	
	
	
	
	/* ***************************************** */
	/* ********* Start of PASS 1 *************** */
	/* ***************************************** */
	
	
	
	
	printf("Starting PASS 1\n");
	fprintf(lstfile,"Starting PASS 1\n");
	
	while (1) {
	
		memset(str1,0,LABELSIZE);
		memset(str2,0,LABELSIZE);
		memset(str3,0,LABELSIZE);
		memset(str4,0,LABELSIZE);
		memset(str5,0,LABELSIZE);
		memset(str6,0,LABELSIZE);
		memset(line, 0, LINESIZE);
		fgets(line, LINESIZE, infile);
		
		if (feof(infile)) break;		// end of file and PASS 1
		linenumber++;					// running count for error reporting
		
		if (line[0] == ';')	continue;	// entire line is a comment - ignore
		if (strlen(line)==0) continue;	// empty line - ignore
		
		
		/* ********** Position 1 - should be a label *********** */
		if (isalpha(line[0])) {			// this line starts with a letter assume is a label
			sscanf(line,"%s %s %s %s %s %s", str1, str2, str3, str4, str5, str6);
			// str1 is the label

			// test if label is a reserved word
			res = isReserved(str1);
			if (res == 1) {	// reserved word. Show error
				printf("Error [1] - Label [%s] in line %d is a reserved word.\n",str1, linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}
			
			char *p = strchr(str1,':');	// test for 'label:' format and strip off and trailing :
			if (p) *p = '\0';
			
			res = isLabel(str1);
			if (res != -1) {		// previously assigned. Show error
				printf("Error [2] - Label [%s] in line %d is previously assigned \n",str1, linenumber);
				printf("%s",line);
				fclose(infile);
				return -1;
			}

			if (res == -1) {		// not assigned yet - assign it
				strcpy(labelname[labelcount], str1);
				labeladdress[labelcount] = address;
				labelcount += 1;
			}
			
			// now test the rest of the line and set the addresses
			
			if (!strcmp(str2,"EQU") || !strcmp(str2,"equ")) {
				// re-assign the label to the value of str3
				if (strlen(str3)==0) {
					printf("Error [3] - Missing value after %s in line %d\n",str2, linenumber);
					printf("%s",line);
					fclose(infile);
					return -1;
				}
				// get the value of str3
				char *p = strchr(str3,'+');		// special case: label EQU label+value
				if (p) {
					*p = ' ';
					char labtest[LABELSIZE]={'\0'}, labvalue[LABELSIZE]={'\0'};
					sscanf(str3,"%s %s",labtest, labvalue);
					int res1, res2;
					res1 = isLabel(labtest);	// returns the address into res1
					if (res1 == -1) {
						printf("Error [4] - Label error in line %d : %s\n",linenumber, labtest);
						printf("%s",line);
						fclose(infile);
						return -1;
					}
					res2 = getAddr(labvalue);
					if (res2 == -1) {
						printf("Error [5] - Value after label in line %d is %d\n",linenumber, res2);
						printf("%s",line);
						fclose(infile);
						return -1;
					}
					// assign value to label
					labeladdress[labelcount-1] = res1 + res2;
				}
				else {	// no +n after value after EQU
					res = isLabel(str3);
					if (res == -1) res = getAddr(str3);
					if (res == -1) {
						printf("Error [6] - Value after EQU in line %d is %d\n",linenumber, res);
						printf("%s",line);
						fclose(infile);
						return 1;
					}
					// re-assign label
					labeladdress[labelcount-1] = res;
				}
				// end of EQU assignment
				
				// test str4
				if (strlen(str4) == 0 || str4[0] == ';') continue;	// comment or null
				
				printf("Error [7] - Unrecognized value [%s] in line %d\n",str4, linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}	// end of EQU tests with label
			
			
			
			// test for DW in str2 (string after label)  Example: COUNTER DW 1280
			if (!strcmp(str2,"dw") || !strcmp(str2,"DW")) {
				// DW is 2 bytes
				address += 2;
				// the value after DW is going to always be a value
				res = isReserved(str3);
				if (res != 0) {
					printf("Error [8] - Bad 16 bit value after %s in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return -1;
				}
				continue;
			} // end of DW tests with label
			
			
			
			// test for DS in str2 (string after label)
			if (!strcmp(str2,"ds") || !strcmp(str2,"DS")) {
				// DS (define storage) value to add to address
				res = isReserved(str3);
				if (res != 0) {
					printf("Error [9] - Reserved word %s after DS in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				res = getAddr(str3);
				if (res == -1) {
					printf("Error [10] - Bad value %s after DW in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				address += res;
				continue;
			}	// TODO: add value of label as value 
			// end of DS tests with label
				
			// DB after label here
			if (!strcmp(str2,"db") || !strcmp(str2,"DB")) {
				// test for DB after label  
				// Example: MSG1 DB CR,LF,"This is a line",CR,LF,NULL
				// DB 0x8e		; insert opcode 8E (unassigned to 8080)

				char dbline[LINESIZE] = {'\0'};
				char labtest[LABELSIZE];
				int labtest_count = 0;
				strcpy(dbline,line); char *p = dbline; 
				// skip all until DB
				while (1) {
					if (*p == 'd' && *(p+1) == 'b') break;
					if (*p == 'D' && *(p+1) == 'B')  break;
					p++;
				}
				// pointing at DB
				p += 2;		// now pointing past DB
				
				memset(labtest,0,LABELSIZE);	// initially clear
				labtest_count = 0;
			
				while (*p != '\0') {
					
					if (isblank(*p)) {
						p++;
						continue;
					}
				
					// Double Quote Test - save everything between "" 
					if (*p == '"') {	// get all between double quotes
						p++;	// skip initial
						// starting quote
						while (1) {
							if (*p == '"') break;	// end of double quotes
							if (*p == '\0') {
								printf("Error [26] - Missing end quotes in line %d\n",linenumber);
								printf("%s",line);
								fclose(infile);
								return 1;
							}
							address += 1;	// each char between "" is 1 byte
							p++;
						}
						p++;	// skip ending double quote
						if (*p == ',') {	// skip comma after ending double quote
							p++;
						}
						continue;
					}
				
					// At comma or line end, test prior word for label, save value
					if (*p == ',' || *p == '\n' || *p == '\0') {	// comma is seperator between labels
						// test if prior block is a label
						res = isLabel(labtest);
						if (res != -1) {
							address += 1;
							if (*p == '\n' || *p == '\0') {		// end of DB statement
								break;
							}
							// assume last char is a comma
							p++;	// skip the ending char
							memset(labtest,0,LABELSIZE);	// initially clear
							labtest_count = 0;
							continue;
						}
						// not a label? maybe a single number
						res = getAddr(labtest);
						if (res != -1) {
							address += 1;
							if (*p == '\n' || *p == '\0') {		// end of DB statement
								break;
							}
							// assume last char is a comma
							p++;
							memset(labtest,0,LABELSIZE);	// initially clear
							labtest_count = 0;
							continue;
						}
						// something before a comma - invalid?
						printf("Error [27] - Unknown value [%s] after DB in line %d\n",labtest, linenumber);
						fclose(infile);
						return 1;
					}
				
					// all other chars save to labtest[n]
					labtest[labtest_count++] = *p;
					p++;
					continue;

					// all other chars are illegal (NOTE: This should never be seen!) 
					printf("Error [28] - Bad character [%c] in DB statement in line %d\n",*p,linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// got EOL
				continue;
			}	// end of DB test
		
		
			
			

			// test for comment or blank after label
			if (str2[0] == ';' || strlen(str2)==0) {
				continue;
			}
			
			
			// test for opcode
			// opcode may be formatted like: in  mov a,b  mvi a,4  lxi h,ADDR  lxi h addr
			res = isOpcode(str2);
			if (res != -1) {	// got an opcode in str1  Example: in, out, nop, hlt
				address += res;	// add size to address
				// done
				continue;
			}
			if (res == -1) {
				char fullopcode[LABELSIZE] = {'\0'};	// Example: mov a,b, ora c, cmp b
				strcpy(fullopcode,str2);
				strcat(fullopcode," ");
				strcat(fullopcode,str3);
				res = isOpcode(fullopcode);
				if (res != -1) {
					address += res;
					continue;
				}
			}
			// opcode may span 2 strings
			if (res == -1) {		// Example: mvi a,3  LXI H test1
				char fullopcode[LABELSIZE] = {'\0'};
				strcpy(fullopcode,str2);
				strcat(fullopcode," ");
				strcat(fullopcode,str3);
				
				// special case: mvi a,1 or any n,x change the , to \0
				char *p = strchr(fullopcode,',');
				if (p) *p='\0';
				
				// now test it
				//printf("split opcode - testing %s\n",fullopcode);
				res = isOpcode(fullopcode);
				if (res != -1) {
					address += res;
					// done
					continue;
				}
			}
			// end of opcode tests with label
			
			// no other in-label tests
		}
		
		/* ********* End of Label (Position 1) tests */
		
		// past the line. str1 is the test string
		sscanf(line,"%s %s %s %s %s %s", str1, str2, str3, str4, str5, str6);
		// str1 is not at position 0, either ORG, DB, opcode, comment
		
		// test for \n etc
		if (str1[0] == '\n' || str1[0] == '\r' || str1[0] == '\0') continue;
		
		// test for comment
		if (str1[0] == ';') {	// comment - skip rest of line
			continue;
		}
		
		// test for org
		if (!strcmp(str1,"org") || !strcmp(str1,"ORG")) {
			// test for reserved (opcodes, etc)
			res = isReserved(str2);
			if (res == 1) {		// str2 is a reserved object
				printf("Error [13] - reserved character %s after ORG statement in line %d\n",str2, linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}
			
			// test for valid number
			res = getAddr(str2);
			if (res != -1) {	// format: org address
				address = res;
				if (address < lowestaddress)
					lowestaddress = address;
				// test str3
				if (strlen(str3) != 0 && str3[0] != ';') {
					printf("Error [14] - Unknown character: %s in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// done here
				continue;
			}
			
			// not a number - test for label
			res = isLabel(str2);
			if (res != -1) {
				address = res;
				if (address < lowestaddress)
					lowestaddress = address;
				// test str3
				if (strlen(str3) != 0 && str3[0] != ';') {
					printf("Error [15] - Unknown character: %s in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// done here
				continue;
			}
			
			// nothing else after org statement
			printf("Error [16] - Unknown character %s after org statement in line %d\n",str2, linenumber);
			printf("%s",line);
			fclose(infile);
			return 1;
		}	// end of ORG test no label
		
		
		
		// test for DB after label  
		if (!strcmp(str1,"db") || !strcmp(str1,"DB")) {
			// test for DB after label  
			// Example: MSG1 DB CR,LF,"This is a line",CR,LF,NULL
			// DB 0x8e		; insert opcode 8E (unassigned to 8080)

			char dbline[LINESIZE] = {'\0'};
			char labtest[LABELSIZE];
			int labtest_count = 0;
			strcpy(dbline,line); char *p = dbline; 
			// skip all until DB
			while (1) {
				if (*p == 'd' && *(p+1) == 'b') break;
				if (*p == 'D' && *(p+1) == 'B')  break;
				p++;
			}
			// pointing at DB
			p += 2;		// now pointing past DB
			
			memset(labtest,0,LABELSIZE);	// initially clear
			labtest_count = 0;
			
			while (*p != '\0') {
				
				if (isblank(*p)) {
					p++;
					continue;
				}
				
				// Double Quote Test - save everything between "" 
				if (*p == '"') {	// get all between double quotes
					p++;	// skip initial
					// starting quote
					while (1) {
						if (*p == '"') break;	// end of double quotes
						if (*p == '\0') {
							printf("Error [26] - Missing end quotes in line %d\n",linenumber);
							printf("%s",line);
							fclose(infile);
							return 1;
						}
						address += 1;	// each char between "" is 1 byte
						p++;
					}
					p++;	// skip ending double quote
					if (*p == ',') {	// skip comma after ending double quote
						p++;
					}
					continue;
				}
				
				// At comma or line end, test prior word for label, save value
				if (*p == ',' || *p == '\n' || *p == '\0') {	// comma is seperator between labels
					// test if prior block is a label
					res = isLabel(labtest);
					if (res != -1) {
						address += 1;
						if (*p == '\n' || *p == '\0') {		// end of DB statement
							break;
						}
						// assume last char is a comma
						p++;	// skip the ending char
						memset(labtest,0,LABELSIZE);	// initially clear
						labtest_count = 0;
						continue;
					}
					// not a label? maybe a single number
					res = getAddr(labtest);
					if (res != -1) {
						address += 1;
						if (*p == '\n' || *p == '\0') {		// end of DB statement
							break;
						}
						// assume last char is a comma
						p++;
						memset(labtest,0,LABELSIZE);	// initially clear
						labtest_count = 0;
						continue;
					}
					// something before a comma - invalid?
					printf("Error [27] - Unknown value [%s] after DB in line %d\n",labtest, linenumber);
					fclose(infile);
					return 1;
				}
				
				// all other chars save to labtest[n]
				labtest[labtest_count++] = *p;
				p++;
				continue;

				
				// all other chars are illegal (NOTE: This should never be seen!) 
				printf("Error [28] - Bad character [%c] in DB statement in line %d\n",*p,linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}
			// got EOL
			continue;
		}	// end of DB test
		
		
		

		
		// test for opcode after label
		// opcode may be formatted like: in  mov a,b  mvi a,4  lxi h,ADDR  lxi h addr
		
		res = isOpcode(str1);	// res returns size or -1
		
		if (res != -1) {	// got an opcode in str1  Example: in, out, nop, hlt
			address += res;	// add size to address
			// done
			continue;
		}
		if (res == -1) {
			char fullopcode[LABELSIZE] = {'\0'};	// Example: mov a,b, ora c, cmp b
			strcpy(fullopcode,str1);
			strcat(fullopcode," ");
			strcat(fullopcode,str2);
			res = isOpcode(fullopcode);
			if (res != -1) {
				address += res;
				continue;
			}
		}
		// opcode may span 2 strings
		if (res == -1) {		// Example: mvi a,3  LXI H test1, rst 1
			char fullopcode[LABELSIZE] = {'\0'};
			strcpy(fullopcode,str1);
			strcat(fullopcode," ");
			strcat(fullopcode,str2);
			
			// special case: mvi a,1 or any n,x change the , to \0
			char *p = strchr(fullopcode,',');
			if (p) *p='\0';
			
			// now test it
			//printf("split opcode - testing %s\n",fullopcode);
			res = isOpcode(fullopcode);
			if (res != -1) {
				address += res;
				// done
				continue;
			}
		}	
		// end of opcode test w/no label
		
		
		// something unknown
		printf("Error [19] - Unknown object [%s] in line %d\n",str1, linenumber);
		printf("%s",line);
		fclose(infile);
		return 1;
	}
	
	
	
	/* ****************************************** */
	/* *********** Done with PASS 1 ************* */
	/* ****************************************** */
	
	
	
	
	printf("Finished PASS 1\n");
	
	fprintf(lstfile,"End of PASS 1\n");
	// fprintf(lstfile, "Ending Address 0x%04X (%d decimal)\n",address, address);
	fprintf(lstfile, "Found %d Labels\n",labelcount);
	
	maxaddress = address;
	
	rewind(infile);
	linenumber = 0;
	
	
	
	
	/* ***************************************** */
	/* ********** Start of Pass 2 ************** */
	/* ***************************************** */
	
	
	
	
	printf("Starting PASS 2\n");
	fprintf(lstfile, "Starting PASS 2\n");
	
	address = lowestaddress;		// reset 
	
	while (1) {
		
		memset(str1,0,LABELSIZE);
		memset(str2,0,LABELSIZE);
		memset(str3,0,LABELSIZE);
		memset(str4,0,LABELSIZE);
		memset(str5,0,LABELSIZE);
		memset(str6,0,LABELSIZE);
		memset(line, 0, LINESIZE);
		fgets(line, LINESIZE, infile);
		
		if (feof(infile)) {		// end of file and PASS 1
			fprintf(lstfile,"End of line in line %d at address %04X\n",linenumber, address);
			break;
		}
		
		linenumber++;					// running count for error reporting
		
		if (line[0] == ';')	continue;	// entire line is a comment - ignore
		if (strlen(line)==0) continue;	// empty line - ignore
		
	
		sscanf(line,"%s %s %s %s %s %s", str1, str2, str3, str4, str5, str6);
		if (str1[0] == '\n' || str1[0] == '\0') continue;
		
			
		// test for org
		if (!strcmp(str1,"org") || !strcmp(str1,"ORG")) {
			// test for reserved (opcodes, etc)
			res = isReserved(str2);
			if (res == 1) {		// str2 is a reserved object
				printf("Error [20] - reserved character %s after ORG statement in line %d\n",str2, linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}
			
			// test for valid number
			res = getAddr(str2);
			if (res != -1) {	// format: org address
				address = res;
				fprintf(lstfile,"ORG %04X  %s\n",address, str2);		// show label too
				// test str3
				if (strlen(str3) != 0 && str3[0] != ';') {
					printf("Error [21] - Unknown character: %s in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// done here
				continue;
			}
			
			// not a number - test for label
			res = isLabel(str2);
			if (res != -1) {
				address = res;
				fprintf(lstfile,"ORG %04X  %s\n",address, str2);
				// test str3
				if (strlen(str3) != 0 && str3[0] != ';') {
					printf("Error [22] - Unknown character: %s in line %d\n",str3, linenumber);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// done here
				continue;
			}
			
			// nothing else after org statement
			printf("Error [23] - Unknown character %s after org statement in line %d\n",str2, linenumber);
			printf("%s",line);
			fclose(infile);
			return 1;
		}	// end of ORG test
		
		// if label ends with ':' strip it off
		char *p = strchr(str1,':');
		if (p) *p = '\0';
		
		// test label - Format LABEL OPCODE VALUE or OPCODE VALUE or something after label
		if (isLabel(str1) != -1) {
			// LABEL opcode/DB/something - ignore label, shift everything down
			if (!strcmp(str2,"equ") || !strcmp(str2,"EQU")) {
				fprintf(lstfile,"%15s\tEQU\t",str1); 
				res = isLabel(str1); 
				fprintf(lstfile,"%04X\n",res);
			} else {
				fprintf(lstfile,"\n%s\t  %04X\n",str1, address);	// print \n before label to make it stand out
			}
			
			strcpy(str1,str2);
			strcpy(str2,str3);
			strcpy(str3,str4);
			strcpy(str4,str5);
			strcpy(str5,str6);
			strcpy(str6,"");
		}
		
		// with label removed, test the following:
		if (str1[0] == '\n' || str1[0] == '\0') continue;		// skip blank lines
		if (str1[0] == ';') continue;		// skip comments
		
		// test for EQU (then skip this line entirely)
		if (!strcmp(str1,"equ") || !strcmp(str1,"EQU")) continue;	// ignore
		 
		// test for DS
		if (!strcmp(str1,"ds") || !strcmp(str1,"DS")) {		// Add 0x00 to memory after DS for each byte
			// str2 will hold either a label or a number
			res = isLabel(str2);
			if (res != -1) {
				for (int n=0; n<res; n++)
					memory[address++] = 0;
				continue;
			}
			// not a label, test for number
			res = getAddr(str2);
			if (res != -1) {
				for (int n=0; n<res; n++)
					memory[address++] = 0;
				continue;
			}

			printf("Error [24] - Unknown character %s after DS statement in line %d\n",str2, linenumber);
			printf("%s",line);
			fclose(infile);
			return 1;
		}
		
		// test for DW
		if (!strcmp(str1,"dw") || !strcmp(str1,"DW")) {		// Add value of label/number to memory
			// str2 will hold either a label or a number
			res = isLabel(str2);
			if (res != -1) {
				memory[address++] = res & 0x00ff;
				memory[address++] = (res & 0xff00) >> 8;
				continue;
			}
			res = getAddr(str2);
			if (res != -1) {
				memory[address++] = res & 0x00ff;
				memory[address++] = (res & 0xff00) >> 8;
				continue;
			}
			printf("Error [25] - Unknown character %s after DW statement in line %d\n",str2, linenumber);
			printf("%s",line);
			fclose(infile);
			return 1;
		}
			
		// test for DB
		if (!strcmp(str1,"db") || !strcmp(str1,"DB")) {
			// test for DB after label  
			// Example: MSG1 DB CR,LF,"This is a line",CR,LF,NULL
			// DB 0x8e		; insert opcode 8E (unassigned to 8080)

			char dbline[LINESIZE] = {'\0'};
			char labtest[LABELSIZE];
			int labtest_count = 0;
			strcpy(dbline,line); char *p = dbline; 
			// skip all until DB
			while (1) {
				if (*p == 'd' && *(p+1) == 'b') break;
				if (*p == 'D' && *(p+1) == 'B')  break;
				p++;
			}
			// pointing at DB
			p += 2;		// now pointing past DB
			fprintf(lstfile,"%04X  DB  ",address);
			
			memset(labtest,0,LABELSIZE);	// initially clear
			labtest_count = 0;
			
			while (*p != '\0') {
				
				if (isblank(*p)) {
					p++;
					continue;
				}
				
				// Double Quote Test - save everything between "" 
				if (*p == '"') {	// get all between double quotes
					p++;	// skip initial
					fprintf(lstfile,"\"");		// starting quote
					while (1) {
						if (*p == '"') break;	// end of double quotes
						if (*p == '\0') {
							printf("Error [26] - Missing end quotes in line %d\n",linenumber);
							printf("%s",line);
							fclose(infile);
							return 1;
						}
						fprintf(lstfile,"%c",*p);
						memory[address] = *p;	// save character in memory
						address += 1;	// each char between "" is 1 byte
						p++;
					}
					fprintf(lstfile,"\" ");		// ending quote
					p++;	// skip ending double quote
					if (*p == ',') {	// skip comma after ending double quote
						p++;
					}
					continue;
				}
				
				// At comma or line end, test prior word for label, save value
				if (*p == ',' || *p == '\n' || *p == '\0') {	// comma is seperator between labels
					// test if prior block is a label
					res = isLabel(labtest);
					if (res != -1) {
						fprintf(lstfile,"%s ",labtest);
						memory[address++] = res;
						if (*p == '\n' || *p == '\0') {		// end of DB statement
							fprintf(lstfile,"\n");
							break;
						}
						// assume last char is a comma
						p++;	// skip the ending char
						memset(labtest,0,LABELSIZE);	// initially clear
						labtest_count = 0;
						continue;
					}
					// not a label? maybe a single number
					res = getAddr(labtest);
					if (res != -1) {
						fprintf(lstfile,"%02X ",res);
						memory[address++] = res;
						if (*p == '\n' || *p == '\0') {		// end of DB statement
							break;
						}
						// assume last char is a comma
						p++;
						memset(labtest,0,LABELSIZE);	// initially clear
						labtest_count = 0;
						continue;
					}
					// something before a comma - invalid?
					printf("Error [27] - Unknown value [%s] after DB in line %d\n",labtest, linenumber);
					fclose(infile);
					return 1;
				}
				
				// all other chars save to labtest[n]
				labtest[labtest_count++] = *p;
				p++;
				continue;

				
				// all other chars are illegal (NOTE: This should never be seen!) 
				printf("Error [28] - Bad character [%c] in DB statement in line %d\n",*p,linenumber);
				printf("%s",line);
				fclose(infile);
				return 1;
			}
			// got EOL
			fprintf(lstfile,"\n");
			continue;
		}	// end of DB test
			
		
		
		// --- test for opcode ---
		
		// opcode may be formatted like: in  mov a,b  mvi a,4  lxi h,ADDR  lxi h addr  mvi a 10
		res = isOpcode(str1);	// res returns -1 or opcode size
		
		// single byte opcode name: in, out, nop, hlt, ret, call, jmp, etc.
		if (res != -1) {	// got an opcode in str1 
			goto SetOpcode;
		}
		
		// test mvi a 10  lxi h address (NOTE: these are special)
		if (!strcmp(str1,"mvi") || !strcmp(str1,"lxi") || !strcmp(str1,"MVI") || !strcmp(str1,"LXI")) {
			char temp[20] = {'\0'};
			// test for mvi a,number
			char *p = strchr(str2,',');
			if (p == NULL) {	// there is NO comma. format: mvi a number
				strcpy(temp,str1); strcat(temp," "); strcat(temp,str2); strcpy(str1, temp);
				strcpy(str2,str3); strcpy(str3,str4); strcpy(str4,str5);
				res = isOpcode(str1);
			}
			// there IS a comma. format mvi a,number
			if (p != NULL) {
				strcpy(temp,str1);	// get the mvi/lxi
				strcat(temp," ");
				char temp2[10] = {'\0'}, temp3[20] = {'\0'};
				*p = ' ';
				sscanf(str2,"%s %s",temp2, temp3);	
				// now have a in temp2 and number in temp3
				strcat(temp,temp2);
				strcpy(str1, temp);
				strcpy(str2,temp3);
			}
			res = isOpcode(str1);
			if (res != -1)
				goto SetOpcode;
			
		}
		
		// opcode is in 2 strings. Example: mov a,b, ora c, cmp b, etc.
		if (res == -1) {
			char fullopcode[LABELSIZE] = {'\0'};	
			strcpy(fullopcode,str1);
			strcat(fullopcode," ");
			strcat(fullopcode,str2);
			res = isOpcode(fullopcode);
			if (res != -1) {
				strcpy(str1,fullopcode); strcpy(str2,str3); strcpy(str3,str4); strcpy(str4,str5);
				goto SetOpcode;
			}
		}
		
		
		/* not an OPCODE */		
		if (res == -1) goto NextPass2Test;	// not an opcode - skip opcode tests below
		
SetOpcode:	// We have an opcode, number in res.
		
		if (res == 1) {		// single byte opcode (ie. nop, mov a,m, hlt, ret)
	
			fprintf(lstfile,"%04X  %02X\t  %s\n",address, getOpCode(str1), str1);
			memory[address++] = getOpCode(str1);	// opcode
			if (strlen(str2)==0 || str2[0] == ';') continue;
		}
		
		if (res == 2) {		// 2 byte opcode (ie. in [value], mvi a [value], cpi [value] )
			int value;
			value = isLabel(str2);
			// test for label as byte 2 of 2 byte opcode
			if (value != -1) {
				fprintf(lstfile,"%04X  %02X  %02X\t  %s %s\n",address, getOpCode(str1), value, str1, str2); 
				memory[address++] = getOpCode(str1);	// opcode
				memory[address++] = value;
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [29] - Unknown character [%s] after 2 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// test for number as byte 2 of 2 byte opcode
			value = getAddr(str2);
			if (value != -1) {
				fprintf(lstfile,"%04X  %02X  %02X\t  %s %s\n",address, getOpCode(str1), value, str1, str2); 
				memory[address++] = getOpCode(str1);	// opcode
				memory[address++] = value;
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [30] - Unknown character [%s] after 2 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// test for single ascii char between 'x': starts with '
			if (str2[0] == '\'') {
				if (str2[1] == '\0') {	// sscanf changes ' ' to string(') string(') and removed the space (0x20)
					str2[1] = ' '; str2[2] = '\0';	// this routine fixes it
					value = 0x20;
					str3[0] = '\0';
				} else {
				value = str2[1];
				}
				fprintf(lstfile,"%04X  %02X  %02X\t  %s %s\n",address, getOpCode(str1), value, str1, str2); 
				memory[address++] = getOpCode(str1);	// opcode
				memory[address++] = value;
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [31] - Unknown character [%s] after 2 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// no other 2 byte opcode values
			printf("Error [32] - Unknown byte [%s] for byte2 of 2 byte opcode in line %d\n",str2, linenumber);
			fclose(infile);
			return 1;
		}
		
		if (res == 3) {		// 3 byte opcode (ie lxi h [value], jmp [value], call [value])
			int value;
			// test for label as byte 2 and 3 of 3 byte opcode
			value = isLabel(str2);
			if (value != -1) {
				fprintf(lstfile,"%04X  %02X  %04X\t  %s %s\n",address, getOpCode(str1), value, str1, str2); 
				memory[address++] = getOpCode(str1);	// opcode
				memory[address++] = value & 0x00ff;				// LSB
				memory[address++] = (value & 0xff00) >> 8;		// MSB
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [33] - Unknown character [%s] after 3 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// test for label with attachments after (ie FILENAME+3)
			char *p = strchr(str2,'+');		// special case: label EQU label+value
			if (p) {
				*p = ' ';
				char labtest[LABELSIZE]={'\0'}, labvalue[LABELSIZE]={'\0'};
				sscanf(str2,"%s %s",labtest, labvalue);
				int res1, res2;
				res1 = isLabel(labtest);	// returns the address into res1
				if (res1 == -1) {
					printf("Error [34] - Label error in line %d : %s\n",linenumber, labtest);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				res2 = getAddr(labvalue);
				if (res2 == -1) {
					printf("Error [35] - Value after label in line %d is %d\n",linenumber, res2);
					printf("%s",line);
					fclose(infile);
					return 1;
				}
				// assign value to label
				fprintf(lstfile,"%04X  %02X",address, getOpCode(str1));
				memory[address++] = getOpCode(str1);	// opcode
				value = res1 + res2;
				fprintf(lstfile,"  %04X\t  %s %s+\n",value, str1, str2);
				memory[address++] = value & 0x00ff;				// LSB
				memory[address++] = (value & 0xff00) >> 8;		// MSB
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [36] - Unknown character [%s] after 3 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// test for number as address
			value = getAddr(str2);
			if (value != -1) {
				fprintf(lstfile,"%04X  %02X  %04X\t  %s %s\n",address, getOpCode(str1), value, str1, str2); 
				memory[address++] = getOpCode(str1);	// opcode
				memory[address++] = value & 0x00ff;				// LSB
				memory[address++] = (value & 0xff00) >> 8;		// MSB
				if (strlen(str3) == 0 || str3[0] == ';') continue;	// end of test on this line
				printf("Error [37] - Unknown character [%s] after 3 byte opcode in line %d\n",str3, linenumber);
				fclose(infile);
				return 1;
			}
			// no other 3 byte opcode values
			printf("Error [38] - Unknown value [%s] for byte 2/3 of 3 byte opcode in line %d\n",str2, linenumber);
			fclose(infile);
			return 1;
		}

NextPass2Test:
					
		// --- Any other tests go here ---	
		
		// all other valid operations are above and continue
		// anything getting this far is an error 
		printf("Error [39] - unknown object [%s][%04X] in line %d\n",str1, str1[0], linenumber);
		printf("At end, not opcode, str1=[%s] str2=[%s] str3=[%s] str4=[%s] str5=[%s]\n",str1, str2, str3, str4, str5);
		fclose(infile);
		return 1;
		
		// end of PASS2 tests
	}
	
	// End of PASS2
	printf("Finished PASS 2\n");
	fprintf(lstfile,"\nFinished PASS 2\n\n");
	
	fclose(infile);
	
	// lst file tail
	fprintf(lstfile, "LABEL LIST: (%d labels)\n",labelcount);
	for (int n=0; n<labelcount; n++)
		fprintf(lstfile, "%15s     0x%04X   (%d)\n",labelname[n], labeladdress[n], labeladdress[n]);
	fprintf(lstfile, "\nStarting/Ending Address %04XH - %04XH\n",lowestaddress, maxaddress);
	fclose(lstfile);
	
	// now write memory dump to comfile
	printf("Writing .com file: %s - ",comfilename);
	comfile = fopen(comfilename,"w");
	if (comfile == NULL) {
		printf("Cannot create %s\n",comfilename);
		return 1;
	}

	// NOTE: only using used addresses (lowestaddress::maxaddress)
	for (int n=lowestaddress; n<maxaddress; n++)
		fprintf(comfile,"%c",memory[n]);
	fclose(comfile);
	printf(" saved.\n");
	
	
	// write .hex file
	/* create Intel hex file from memory */
	int checksum = 0;
	int filelength = maxaddress;
	int runaddress = lowestaddress;		// 0;
	int adr = lowestaddress;  // 0;
	int cc = 0, cnt = 0;
	uint8_t addrhi, addrlo, byteval;
	hexfile = fopen(hexfilename,"w");
	if (hexfile == NULL) {
		printf("Error creating hexfile %s\nStopping.\n",hexfilename);
		return 1;
	}
	/* start conversion from binary to hex */
	while (filelength - adr > 0) {
		fprintf(hexfile,":");
		if (filelength - adr >= 16)
			cc = 16;
		else
			cc = filelength - adr;
		/* start checksum for this line */
		checksum = cc;
		byteval = cc;
		/* write byte count to file */
		fprintf(hexfile,"%2.2X",byteval);
		/* convert address to 2 bytes */
		addrhi = (adr & 0xff00) >> 8;
		addrlo = adr & 0x00ff;
		/* save address to hex file */
		fprintf(hexfile,"%2.2X",addrhi);
		fprintf(hexfile,"%2.2X",addrlo);
		/* add to running checksum */
		checksum += addrhi;
		checksum += addrlo;
		/* this is a normal record, output type */
		fprintf(hexfile,"%2.2X",0);
		/* get output and data types */
		for (int n=0; n != cc; n++) {
			byteval = memory[runaddress++];
			fprintf(hexfile,"%2.2X",byteval);
			checksum += byteval;
			adr++;
		}
		byteval = ~checksum + 1;		// two's compliment
		fprintf(hexfile,"%2.2X",byteval);
		fprintf(hexfile,"\n");
	}
	/* mem bytes all read, create last record of :00000001FF */
	fprintf(hexfile,":00000001FF\n");		// bytecount addrhi addrlo endchar checksum
	/* done */
	fclose(hexfile);
	printf("Hex file Written\n");
	
	
	
	return 0;
}
			
	
	
