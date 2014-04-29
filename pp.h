
#ifndef _PP_H_
#define _PP_H_

#define PP_SUCCESS            0
#define PP_FAIL               -1
#define PP_HANDLE_SIZE        4
#define PP_MAX_USER_TYPES     8
#define PP_EXHAUSTION        -999999999
#define PP_NO_MORE_WORK      -999999998

int PP_Init(int,int*,int);
int PP_Finalize(void);
int PP_Put(void*,int,int,int);
int PP_Reserve(int,int*,int*,int*,int*);
int PP_Get(void*,int*);
int PP_Set_problem_done(void);
int PP_Abort(int);

#endif
