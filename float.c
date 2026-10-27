/* float.c - floating point math routines for emulator_x */
/* (C) 2026 K Theis  */

#include "emulator.h"


extern uint8_t read_memory(uint8_t, uint16_t);
extern uint8_t write_memory(uint8_t, uint16_t, uint8_t);


/* global externals */
extern I80REGS regs[MAXUSER];
//extern uint8_t memory[MAXRAM][MAXUSER];
extern uint8_t rom[];
extern uint8_t ram[];

#pragma GCC push_options
#pragma GCC optimize ("O0")
int float_routine(uint8_t opcode, uint8_t user) {

	// opcode is the function number
    
    // BC points to op1, the 1st operand for floating point operations
    // DE points to op2, the 2nd operand for floating point operations
    // HL points to the floating point result
    
    uint16_t adr1 = (regs[user].B << 8) | (regs[user].C);
    uint16_t adr2 = (regs[user].D << 8) | (regs[user].E);
    uint16_t adrr = (regs[user].H << 8) | (regs[user].L);
    
    // get full 32-bit (4-byte) raw values from 8080 memory (Little-Endian)
    uint32_t raw_op1 = read_memory(user, adr1 + 0)         | 
                      (read_memory(user, adr1 + 1) << 8)  | 
                      (read_memory(user, adr1 + 2) << 16) | 
                      (read_memory(user, adr1 + 3) << 24);


    uint32_t raw_op2 = read_memory(user, adr2 + 0)         | 
                      (read_memory(user, adr2 + 1) << 8)  | 
                      (read_memory(user, adr2 + 2) << 16) | 
                      (read_memory(user, adr2 + 3) << 24);

	uint32_t raw_res;		// will assign later

    float val1, val2, result = 0.0f;
    
    // Copy directly from the raw_op variables into floats
    memcpy(&val1, &raw_op1, sizeof(float)); 
    memcpy(&val2, &raw_op2, sizeof(float));
    
    switch (opcode) {
    	case 0x00:		// Unassigned	
    		break;	

       case 0x01:    // ADD
        	result = val1 + val2;
        	printf("add: %f + %f = %f\n", val1, val2, result);
            break;
            
        case 0x02:    // SUB
            result = val1 - val2;
            printf("sub: %f - %f = %f\n", val1, val2, result);
            break;
            
        case 0x03:    // MUL
            result = val1 * val2;
            printf("mul: %f * %f = %f\n", val1, val2, result);
            break;
            
        case 0x04:    // DIV
            if (val2 == 0) {	// check for division by zero
            	printf("Division by 0: %f/%f\n",val1, val2);
            	return 1;
            }
            result = val1 / val2;
            printf("div: %f / %f = %f\n", val1, val2, result);
            break;
            
        case 0x05:		// SQR
        	if (val1 < 0) {		// positive values only
        		printf("Square Root of a negative value %f\n",val1);
        		return 1;
        	}
        	result = sqrt(val1);
        	break;
            
        case 0xfe:	// Float to String   
        {
        	// Convert an IEEE754 floating point value to a NULL terminated string
        	// BC points to 32 bit float value
        	// HL points to NULL terminated string in memory
        	volatile int n=0;
        	char stringfs[80] = {'\0'};
        	sprintf(stringfs,"%f",val1);

        	// adrr (address of HL) 
        	for (n = 0; n<80; n++) {
        		if (stringfs[n] == '\0') break;
        		write_memory(user, adrr + n, stringfs[n]);
        	}
        	// Mo flags to worry about
        	return 0;
         }
            
        case 0xff:	// String to Float
        {
        	// Convert a NULL terminated string to an IEEE754 floating point value
        	// BC points to a null terminated string with a decimal number in it (ie 12345.67)
        	// HL points to the resulting 32 bit floating point value     
        	volatile int n=0;
        	float f;
        	char fpstring[80] = {'\0'};
        	for (n=0; n<80; n++) {
        		if (read_memory(user, adr1 + n) == '\0') {
        			break;
        		}
       			fpstring[n] = read_memory(user, adr1 + n);
       		}

       		// string to float conversion
       		f = atof(fpstring);
       		
       		// float to IEEE754 conversion
       		unsigned int *binary_rep = (unsigned int *)&f;
       		
       		// Copy float result back into a 32-bit int variable
       		memcpy(&raw_res, binary_rep, sizeof(float));
       		
       		// Break the 32-bit result back down into 4 bytes in 8080 memory (Little-Endian)
    		write_memory(user, adrr, (uint8_t)(raw_res & 0xFF));
    		write_memory(user, adrr + 1, (uint8_t)((raw_res >> 8) & 0xFF));
    		write_memory(user, adrr + 2, (uint8_t)((raw_res >> 16) & 0xFF));
    		write_memory(user, adrr + 3, (uint8_t)((raw_res >> 24) & 0xFF));
    
    		// Update flags based on the float result
    		regs[user].Z = (result == 0.0f);
    		regs[user].S = (result < 0.0f); // Note: sign bit also handles negative zero (-0.0f) 
    		// Ignoring P, CY and AC since this spans 4 bytes
    		
    		return 0;		// done here

       	}
       		
            
            
        default:
            printf("Bad Opcode %02X in Floating Point Operation\n", opcode);
            return 1; // Return error on bad opcode
    }
    
    // Copy float result back into a 32-bit int variable
    
    memcpy(&raw_res, &result, sizeof(float));
    
    // Break the 32-bit result back down into 4 bytes in 8080 memory (Little-Endian)
    write_memory(user, adrr, (uint8_t)(raw_res & 0xFF));
    write_memory(user, adrr + 1, (uint8_t)((raw_res >> 8) & 0xFF));
    write_memory(user, adrr + 2, (uint8_t)((raw_res >> 16) & 0xFF));
    write_memory(user, adrr + 3, (uint8_t)((raw_res >> 24) & 0xFF));
    
    // Update flags based on the float result
    regs[user].Z = (result == 0.0f);
    regs[user].S = (result < 0.0f); // Note: sign bit also handles negative zero (-0.0f) 
    // Ignoring P, CY and AC since this spans 4 bytes
    
    return 0;
}
#pragma GCC pop_options














    	
