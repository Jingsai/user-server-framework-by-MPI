#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "mpi.h"
#include "pp.h"
#include "queue.h"

#define True 1
#define False 0
#define MAX_MEMORY_SPACE 1000000000  // 10^9 byte
#define MAX_WORK_TYPE 64  // value based on the test file 
#define ANY_WORK_TYPE 99990

MPI_Comm comm, intercomm; // comm is local, intercomm communicate between user and server
MPI_Request request;
MPI_Status status;
int flag = -1;
int rankworld, rank, sizeworld, size, num_servers;
// Length is the size of each job unit
// put_rank_acount for put the job to the server one by one
// reserve_rank_acount for reserve the job to the server one by one
// FirstReserve is for sometime the user need wait for sometime until some job really be put in
int Length = 0, put_rank_count = 0, reserve_rank_count = 0, FirstReserve = 0;
// done is that when one user find job done, tell all the server. but this is only work for only one user.
// stop is one user can not reesrve available job, tell all server. when all user stop, the whold job done. then 
//      server can return
// JobRecv and JobReserve both for check if receive and reserve sucessfull or fail
int done = False, JobRecv = False, JobReserve = False, stop = False;

int PP_Init(int num_user_types,int* user_types,int flag)
{
    MPI_Request stoprequest, donerequest, putrequest[2], reserverequest, getrequest, request;
    MPI_Status stopstatus, donestatus, putstatus[2], reservestatus, getstatus, status;
    int stopflag = -1, doneflag = -1, putflag[2] = {-1,-1}, reserveflag = -1, getflag = -1;

    MPI_Comm_rank(MPI_COMM_WORLD, &rankworld);
    MPI_Comm_size(MPI_COMM_WORLD, &sizeworld);
   /* 
    char name[100]; int len;
    MPI_Get_processor_name(name, &len);
    printf("rank %d flag %d name %s\n", rankworld, flag, name);
   */
    MPI_Allreduce(&flag, &num_servers, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    // split and link
    MPI_Comm_split(MPI_COMM_WORLD, flag, rankworld, &comm );
    if (flag == 1)
        MPI_Intercomm_create( comm, 0, MPI_COMM_WORLD, 0, 1, &intercomm); 
    else
        MPI_Intercomm_create( comm, 0, MPI_COMM_WORLD, sizeworld-num_servers, 1, &intercomm); 
  
    MPI_Comm_rank( intercomm, &rank );
    MPI_Comm_remote_size( intercomm, &size );

    // user return, while server stay until all job done
    if (flag == 0)
        return PP_SUCCESS;

    // count how many users done to decide if all users done, then all servers can return
    int client_count = 0 ,client_num = sizeworld - num_servers;
    // buf for put the job, malloc it when pp_put, free it inside queue.c via free(oldPtr->buf)
    // reserve_buf for temporary get the job from queue and send to user, free it after all job done.
    char* buf, * reserve_buf;
    // request and get handle. 1-4: server_rank, null, work_length, work_type
    int reqHandle[4], getHandle[4];
    // puttype[1] is 1 if type is answer, otherwise puttype[0] is the work_type
    // req_type pass from the user to reserve if the servers have such work, at most MAX_WORK_TYPE
    int i, put_type[2], req_type[MAX_WORK_TYPE];
    // work of type A store in workqueue[A]
    Queue* workqueue[MAX_WORK_TYPE]; 
    // check if we need to init the queue[A] when job A come in, init at the first time call
    int workqueue_initflag[MAX_WORK_TYPE]; // 0 or 1
    // ANSWER store the answer integer number passed from user
    // ANSWER is the total number of answer_job in one server
    // ANSWER_RESERVE is to check if the work reserved by user is an answer_job 
    int ANSWER, ANSWER_NUM = 0, ANSWER_RESERVE = False; 
    // num_work_units count how many units of job have been malloc, can not bigger then max, otherwise abort
    int num_work_units=0, max_num_work_units;
    // put_source need two source value because user send buf and type_work via two send, wervers need to 
    //        check if the buf and work_type from the same source
    // reserve and get source used by server to decide who send me the signal 
    int put_source[2], reserve_source, get_source;

    // servers receive the length of the job unit
    MPI_Recv(&Length, 1, MPI_INT, MPI_ANY_SOURCE, 2, intercomm, &status);
    // calculate how many jobs can be put in one server
    max_num_work_units = MAX_MEMORY_SPACE/Length;
    // init the flags of queue
    for (i=0; i<MAX_WORK_TYPE; i++)
        workqueue_initflag[i] = 0;
    // init the temporary buf and free it after all jobs done
    reserve_buf = (char *) malloc(Length);

    // all servers run inside this while loop until user send done    or     server find that all users
    //      return by failed reserve. here user will send a stop to servers when this user failed reserve
    //      and return
    // each server keep wait for two types of signals: put or reserve, while get is inside reserve
    // servers use non-block Irecv to receive the signal, then server can skip to accept other signal
    //      then use Test to check if the server really get the signal. then the flag be 1 and continue 
    //      do the server things. change back to -1 to let the server go to wait the singal again. 
    while (!done)
    {
        // count how many uses failed reserve and return. serves stop loop if all users return
        while (stopflag != 0)
        {
            MPI_Irecv(&stop, 1, MPI_INT, MPI_ANY_SOURCE, 11, intercomm, &stoprequest);
            stopflag = 0;
        }
        MPI_Test(&stoprequest, &stopflag, &stopstatus);
        if (stopflag != 0)
        {
           client_count++;
           if (client_count >= client_num)
                done = 1;
        }

        // wait for the done signal, if one user find jobs done  
        while (doneflag != 0)
        {
            MPI_Irecv(&done, 1, MPI_INT, MPI_ANY_SOURCE, 1, intercomm, &donerequest);
            doneflag = 0;
        }
/*
        // following is useless, because done will be receive anywhere inside this whole while loop, and done value will prabably not change after the following if.
        MPI_Test(&donerequest, &doneflag, &donestatus);
        if (doneflag != 0)
            doneflag = -1;
*/
        // put the job into the queue
        while (putflag[0] != 0 && putflag[1] != 0)
        {
            buf = (char *) malloc(Length);
            MPI_Irecv(buf, Length, MPI_CHAR, MPI_ANY_SOURCE, 3, intercomm, &putrequest[0]); 
            MPI_Irecv(&put_type, 2, MPI_INT, MPI_ANY_SOURCE, 33, intercomm, &putrequest[1]); 
            putflag[0] = 0;
            putflag[1] = 0;
        }
        MPI_Test(&putrequest[0], &putflag[0], &putstatus[0]);
        MPI_Test(&putrequest[1], &putflag[1], &putstatus[1]);
        if (putflag[0] !=0 && putflag[1] !=0)
        {
            if (put_type[1] == 0)  // not answer
            {
                if (workqueue_initflag[put_type[0]] == 0)
                {
                    workqueue[put_type[0]] = InitQueue();
                    workqueue_initflag[put_type[0]] = 1;
                }
                // check if server is full
                num_work_units++;
                if (num_work_units <= max_num_work_units) 
                {
                    JobRecv = True;
                    putQueue(workqueue[put_type[0]], buf, put_type[0]);
                }
                else
                    JobRecv = False; // send false if failed put in
            }
            else // answer
            {
                JobRecv = True;
                ANSWER = put_type[0];  // recode the answer integer
                ANSWER_NUM++; // increase how many answer_job in this server
            }
            // sometime, one source is -2, while another is 0 
            // in pstest2, sometime two source is different, if only send one source, the real source may wait for the response
            put_source[0] = putstatus[0].MPI_SOURCE;
            put_source[1] = putstatus[1].MPI_SOURCE;
            if (put_source[0] != put_source[1]) 
            {
                if (put_source[0] >= 0)
                    MPI_Send(&JobRecv, 1, MPI_INT, put_source[0] , 333, intercomm);
                if (put_source[1] >= 0)
                    MPI_Send(&JobRecv, 1, MPI_INT, put_source[1] , 333, intercomm);
            }
            else
                MPI_Send(&JobRecv, 1, MPI_INT, put_source[0] , 333, intercomm);
            putflag[0] = -1;
            putflag[1] = -1;
        }

        // reserve and get
        // answer do not need to get
        while (reserveflag != 0)
        {
            MPI_Irecv(&req_type, MAX_WORK_TYPE, MPI_INT, MPI_ANY_SOURCE, 4, intercomm, &reserverequest);
            reserveflag = 0;
        }
        MPI_Test(&reserverequest, &reserveflag, &reservestatus);
        if (reserveflag != 0)
        {
            JobReserve = False;  // init
            ANSWER_RESERVE = False; // init
            reserve_source = reservestatus.MPI_SOURCE; // check who send me signal
            // if any_work_type, check all available job in this server
            if (req_type[0] == ANY_WORK_TYPE)
            {
                // check if this server has answer_job firstly
                if (ANSWER_NUM)
                {
                    ANSWER_RESERVE = True; // success 
                    JobReserve = True;  // success
                    reqHandle[0] = rank;
                    reqHandle[2] = 0;  // length of answer is 0
                    reqHandle[3] = ANSWER;
                    ANSWER_NUM--;
                } 
                // check all possible job
                else
                {
                    for (i=0; i<MAX_WORK_TYPE; i++)
                    {
                        if (workqueue_initflag[i] != 0 && !IsEmpty(workqueue[i]))
                        {
                            reqHandle[0] = rank;
                            reqHandle[2] = Length;
                            reqHandle[3] = i;
                            break;
                        }
                    }
                    // no job found if i bigger then max
                    if (i < MAX_WORK_TYPE)
                        JobReserve = True;
                }
            }
            // check the reserve job types from users
            else
            {
                // loop over the request of the user
                for (i=1; i<=req_type[0]; i++)
                {
                    if (req_type[i] != ANSWER)
                    {
                        if (workqueue_initflag[req_type[i]] != 0 && !IsEmpty(workqueue[req_type[i]]))
                        {
                            JobReserve = True;
                            reqHandle[0] = rank;
                            reqHandle[2] = Length;
                            reqHandle[3] = req_type[i];
                            break;
                        }
                    }
                    else
                    {
                        if (ANSWER_NUM)
                        {
                            ANSWER_RESERVE = True;
                            JobReserve = True;
                            reqHandle[0] = rank;
                            reqHandle[2] = 0;
                            reqHandle[3] = ANSWER;
                            ANSWER_NUM--;
                            break;
                        } 
                    }
                }
            
            }
            // if reserve successfully, send signal and handle to user
            // wait for this user come back to get if the job is a buf, not an answer
            if (JobReserve)
            {
                MPI_Send(&JobReserve, 1, MPI_INT, reserve_source, 55, intercomm);
                MPI_Send(reqHandle, 4, MPI_INT, reserve_source, 5, intercomm);

                if (ANSWER_RESERVE == False)
                {
                    // wait for get
                    MPI_Recv(getHandle, 4, MPI_INT, reserve_source, 6, intercomm, &getstatus);
                        getQueue(workqueue[getHandle[3]], reserve_buf);
                        // total work number this server can load should -1
                        num_work_units--;
                        MPI_Send(reserve_buf, Length, MPI_CHAR, reserve_source, 7, intercomm);
                }
            }
            else
            {
                //JobReserve = False;
                MPI_Send(&JobReserve, 1, MPI_INT, reserve_source, 55, intercomm);
            }
            reserveflag = -1;
        }
    }
    free(reserve_buf);
    return PP_SUCCESS;    
}

// call by user to put job
int PP_Put(void* buf,int length,int type,int target_rank)
{
    // target_rank is the rank of user, not the server rank that user wants to put in
    int put_rank, count, i, j;
    
    // loop over all server until put in succeffully
    for (count=0; count<num_servers; count++) 
    {
        // put one by one
        put_rank = put_rank_count % num_servers;
        put_rank_count++;
        // send the length at the first time
        if (Length == 0)
        {
            Length = length;
            for (i=0; i<num_servers; i++)
                MPI_Send(&Length, 1, MPI_INT, i, 2, intercomm);   
        }
        // send buf and job type, when length is 0, is answer, type[1]=1
        MPI_Send(buf, length, MPI_CHAR, put_rank, 3, intercomm);
        int jobtype[2];
        jobtype[0] = type;
        if (length != 0)
        {
            jobtype[1] = 0;
            MPI_Send(&jobtype, 2, MPI_INT, put_rank, 33, intercomm);
        }
        else
        {
            jobtype[1] = 1;
            MPI_Send(&jobtype, 2, MPI_INT, put_rank, 33, intercomm);
        }
        // check if really put in this server
        MPI_Recv(&JobRecv, 1, MPI_INT, put_rank, 333, intercomm, &status);
        if (JobRecv == True)
            return PP_SUCCESS;    
    }
    // can not put in any server, abort
    return PP_EXHAUSTION;
}

// call by user, most important function to control communication and when stop
int PP_Reserve(int number_req ,int* types_req,int* work_len,int* work_type,int* handle)
{
    // tell client done, then client can exit from the while
    if (done)
       return PP_NO_MORE_WORK;

    int i, count, reserve_rank, type[MAX_WORK_TYPE];
    if (number_req == 0)  //type = ANY_WORK_TYPE if 0
        type[0] = ANY_WORK_TYPE; 
    else
    {
        type[0] = number_req;
        for (i=0; i<number_req; i++)
        {
            type[i+1] = types_req[i];
        }
    }

    // other usres do more loops if jobs have not been put in by the master user
    int ReserveRepeat = 1;
    if (FirstReserve < 1000)
        ReserveRepeat = 500;
    FirstReserve++;

    for (i=0; i<ReserveRepeat; i++)
    {
        // loop over all servers until reserve successfully
        for (count=0; count<num_servers; count++) 
        {
            reserve_rank = reserve_rank_count % num_servers;
            reserve_rank_count++;

            MPI_Send(&type, MAX_WORK_TYPE, MPI_INT, reserve_rank, 4, intercomm);
            MPI_Recv(&JobReserve, 1, MPI_INT, reserve_rank, 55, intercomm, &status);
            // if reserve successfully, receive the handle
            if (JobReserve == True)
            {
                MPI_Recv(handle, 4, MPI_INT, reserve_rank, 5, intercomm, &status);
                *work_len = handle[2];
                *work_type = handle[3];
                return PP_SUCCESS;    
            }
        }
    }
    // when num_work_unit is 1, sometimes the user go to reserve before the only one job has benn finished to put in the queue by the server. no such problem if num_work_unit is bigger than 2.
    // or in pstest1, one client go to reserve before master client put the job. 

    // tell servers that I am stop. servers stop if all users stop 
    for (count=0; count<num_servers; count++) 
    {
        MPI_Send(&stop, 1, MPI_INT, count, 11, intercomm); 
    }
    return PP_NO_MORE_WORK;    
}

// call by uses to get the buf job after succeffully reserve
int PP_Get(void* buf,int* handle)
{
    // tell client done
    if (done)
       return PP_NO_MORE_WORK;
    
    MPI_Send(handle, 4, MPI_INT, handle[0], 6, intercomm);
    MPI_Recv(buf, handle[2], MPI_CHAR, handle[0], 7, intercomm, &status);

    return PP_SUCCESS;    
}

// if one user find that all jobs done, tell all servers done
int PP_Set_problem_done(void)
{
    int count;
    done = 1;
    for (count=0; count<num_servers; count++) 
    {
        MPI_Send(&done, 1, MPI_INT, count, 1,  intercomm);
    }
    return PP_NO_MORE_WORK; 
}

// abort is overflow, which is that server is full
int PP_Abort(int code)
{
    printf("*****************************\n");
    printf("*                           *\n");
    printf("*  Abort with the code %d   *\n", code);
    printf("*                           *\n");
    printf("*****************************\n");

    MPI_Abort(MPI_COMM_WORLD, 911);
    return PP_FAIL;    
}

// finalize when all jobs finish
int PP_Finalize(void)
{
    MPI_Comm_free(&comm);
    MPI_Comm_free(&intercomm);
    return PP_SUCCESS;    
}

