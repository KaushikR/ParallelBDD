//////////////////////////////////////////////////////////////////////
// ROBDD Implementation - Static Variable Ordering
// Kaushik Rangarajan
// To compile - g++ bdd.cpp -std=gnu++11 -pthread
/////////////////////////////////////////////////////////////////////
#include "tables.cpp"

////////////////////////////////////////////////////////////////////////
// Global Variables
////////////////////////////////////////////////////////////////////////

#define RETURN '\n'
#define EOS '\0'
#define SPACE ' '
#define scramble 5

char vector[100];					// Array for storing vectors
char cktName[10];
CompTable *CT;						// The ComputedTable (Ze cache)
HashTable *HT;					    // The hash table
char vecName[10],bddName[10];
int vecWidth, gateNum;
FILE *vecFile;
pthread_mutex_t mutexsim;			// Mutex for simulation
pthread_mutex_t mutexcnt;			// Mutex for counting
void *RUNBDD(void*);				// The parallel function which the threads run
int* VariableOrder;
int globalWindow = 0;

enum
{
   G_JUNK,         /* 0 */
   G_INPUT,        /* 1 */
   G_OUTPUT,       /* 2 */
   G_XOR,          /* 3 */
   G_XNOR,         /* 4 */
   G_DFF,          /* 5 */
   G_AND,          /* 6 */
   G_NAND,         /* 7 */
   G_OR,           /* 8 */
   G_NOR,          /* 9 */
   G_NOT,          /* 10 */
   G_BUF,          /* 11 */
};

////////////////////////////////////////////////////////////////////////
// circuit class
////////////////////////////////////////////////////////////////////////

class circuit
{
    int numgates;	       // total number of gates (faulty included)
    int maxlevels;         // number of levels in gate level ckt
    unsigned char *gtype;  // gate type
    short *fanin;		   // number of fanins of gate
    short *fanout;		   // number of fanouts of gate
    int *levelNum;		   // level number of gate
    int **inlist;	   	   // fanin list of gate
    int **fnlist;	       // fanout list of gate

public:	

    // Variables
    int numInputs;		   				// number of inputs
    int numOutputs;		   				// number of outputs
	int outputs[500];	   				// outputs list
	unsigned long numNodes;				// To count number of nodes reqd	
	node **nodes;		   				// Contains top node for each gate
	short *var_ord;		   				// Contains the variable ordering

	// Functions
    circuit(char *);       			    // constructor
	void bdd(int);					    // build BDD
	node *ite(node *,node *,node *);    // ITE Block
	int findtop(node *,node *, node *); // To find top variable
	node *if_cofactor(int, node *);		// To help with 'if' co-factoring
	node *else_cofactor(int, node *);	// To help with 'else' co-factoring
	node* NOT(node *temp);				// To negate a node
	void bddsim(int);					// To produce ouput vectors
	void logicSimFromFile(FILE *, int);	// To simulate the BDD
	void bddprint(FILE *);				// To print the BDD files onto a txt file
	void setVariableOrder(int);			// To get a random variable order [As of now manually entered, in the future automatic methods will be used]
	void shuffle(int, int);             // To shuffle the variable ordering
	void count(node*);					// Function used to count nodes
	void resetcount(node*);				// Function used to reset nodes' visited
};

////////////////////////////////////////////////////////////////////////
// constructor: reads in the *.lev file and builds basic data structure
// 		for the gate-level ckt
////////////////////////////////////////////////////////////////////////
circuit::circuit(char *cktName)
{
    FILE *inFile;
    char fName[40];
    int i, j, count;
    char c;
    int gatenum, junk;
    int f1, f2, f3;
	
    strcpy(fName, cktName);
    strcat(fName, ".lev");
    inFile = fopen(fName, "r");
    if (inFile == NULL)
    {
	fprintf(stderr, "Can't open .lev file\n");
	exit(-1);
    }

    numgates = maxlevels = 0;
    numInputs = numOutputs = 0;

    fscanf(inFile, "%d", &count);	// number of gates
    fscanf(inFile, "%d", &junk);	// skip the second line

    // allocate space for gates data structure
    gtype = new unsigned char[count];
    fanin = new short[count];
    fanout = new short[count];
    levelNum = new int[count];
    inlist = new int * [count];
    fnlist = new int * [count];	
	nodes = new node*[count];	// To Keep Track of Top Node of ROBDD for each gate
	numNodes = 0;				// To count nodes per BDD
		
    // now read in the circuit
    for (i=1; i<count; i++)
    {	
	fscanf(inFile, "%d", &gatenum);
	fscanf(inFile, "%d", &f1);
	fscanf(inFile, "%d", &f2);
	fscanf(inFile, "%d", &f3);
	numgates++;
	gtype[gatenum] = (unsigned char) f1;
	if (gtype[gatenum] > 13)
	    printf("gate %d is an unimplemented gate type\n", gatenum);
	else if (gtype[gatenum] == G_INPUT)
	    numInputs++;
	else if (gtype[gatenum] == G_OUTPUT)
	{
		outputs[numOutputs] = gatenum;							// To keep track of output nodes
		numOutputs++;
		//cout<<outputs[0];
	}

	f2 = (int) f2;
	levelNum[gatenum] = f2;

	if (f2 >= (maxlevels))
	    maxlevels = f2 + 5;

	fanin[gatenum] = (int) f3;
	// now read in the inlist
	inlist[gatenum] = new int[fanin[gatenum]];
	for (j=0; j<fanin[gatenum]; j++)
	{
	    fscanf(inFile, "%d", &f1);
	    inlist[gatenum][j] = (int) f1;
	}

	for (j=0; j<fanin[gatenum]; j++) // followed by samethings
	    fscanf(inFile, "%d", &junk);

	// read in the fanout list
	fscanf(inFile, "%d", &f1);
	fanout[gatenum] = (int) f1;

	fnlist[gatenum] = new int[fanout[gatenum]];
	for (j=0; j<fanout[gatenum]; j++)
	{
	    fscanf(inFile, "%d", &f1);
	    fnlist[gatenum][j] = (int) f1;
	}

	// skip till end of line
	while ((c = getc(inFile)) != '\n' && c != EOF)
	    ;
    }	// for (i...)
    fclose(inFile);

    //printf("Successfully read in circuit:\n");
    //printf("\t%d PIs.\n", numInputs);
    //printf("\t%d POs.\n", numOutputs);
    //printf("\t%d total number of gates\n", numgates);
    //printf("\t%d levels in the circuit.\n", maxlevels / 5);
}

//////////////////////////////////////////////////////////////////////
//// Sets a random variable order 
//////////////////////////////////////////////////////////////////////
void circuit :: setVariableOrder(int window)
{
	int i;
	for (i=1; i<=numInputs; i++)
		var_ord[i] = i;

	var_ord[numInputs+10] = numInputs + 10;
	shuffle((window*scramble)+1,((window+1)*scramble));
	//shuffle(1,5);
}

//////////////////////////////////////////////////////////////////////
//// A Fisher-Yates shuffle to randomize the variable ordering
//////////////////////////////////////////////////////////////////////
void circuit :: shuffle(int low, int high)
{
	int i, max;
	for (i=high; i>=low; --i)
	{
		int random_number = (rand() % (high-low+1)) + low;
		max = var_ord[i];
		var_ord[i] = var_ord[random_number];
		var_ord[random_number] = max;
	}

	//for (i=1; i<=numInputs; i++)
		//cout<<var_ord[i]<<' ';
	//cout<<endl;
}			

//////////////////////////////////////////////////////////////////////
// Negates a Node & returns the negated node
//////////////////////////////////////////////////////////////////////
node* circuit :: NOT(node *temp)
{
	if( temp == one )
		return zero;
	if( temp == zero )
		return one;
	if( temp-> high == one && temp-> low == zero)
	{
		if(temp-> sign == false)
		return(HT->findoradd(temp->var,true, one, zero));
		if(temp-> sign == true)
		return(HT->findoradd(temp->var,false, one, zero));
	}
	if( temp-> high == zero && temp-> low == one)
	{
		if(temp-> sign == true)
		return(HT->findoradd(temp->var, true, one, zero));
		if(temp-> sign == false)
		return(HT->findoradd(temp->var,false, one, zero));		
	}
	return(HT->findoradd(temp->var,!(temp->sign), temp->high, temp->low));
}

////////////////////////////////////////////////////////////////////////
// Function to build the BDD
////////////////////////////////////////////////////////////////////////
void circuit::bdd(int window)
{
	int i,j;
	node *temp;
	
	zero -> var = numInputs + 10;			// Node zero initialization
	zero -> low = NULL;
	zero -> high = NULL;
	zero -> next = NULL;
	zero -> index = 1;
	
	one -> var = numInputs + 10;			// Node one initialization
	one -> low = NULL;
	one -> high = NULL;
	one -> next = NULL;
	one -> index = 2;
	
	var_ord = new short[numInputs+11];
	setVariableOrder(window);
	
	for ( i = 1 ; i <= numgates ; i++)
	{	
		switch(int(gtype[i]))
		{
			case G_INPUT :
				nodes[i] = HT -> findoradd(i, false, one, zero);			
			break;
			case G_XOR :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp,  NOT(nodes[inlist[i][j+1]]), nodes[inlist[i][j+1]]);
				nodes[i] = temp;
			break;
			case G_XNOR :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp,  nodes[inlist[i][j+1]], NOT(nodes[inlist[i][j+1]]));
				nodes[i] = temp;
			break;
			case G_AND :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp, nodes[inlist[i][j+1]], zero);
				nodes[i] = temp;
			break;
			case G_NAND :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp, nodes[inlist[i][j+1]], zero);
				nodes[i] = NOT(temp);
			break;
			case G_OR :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp,  one, nodes[inlist[i][j+1]]);
				nodes[i] = temp;			
			break;
			case G_NOR :
				temp = nodes[inlist[i][0]];
				for ( j = 0 ; j < fanin[i] - 1; j ++)
					temp = ite(temp,  one, nodes[inlist[i][j+1]]);
				nodes[i] = NOT(temp);
			break;
			case G_NOT :
			nodes[i] = NOT(nodes[inlist[i][0]]);
			break;	
			default :
			nodes[i] = nodes[inlist[i][0]];	
		}
	}
}
	
//////////////////////////////////////////////////////////////////
// To find the Top Variable
/////////////////////////////////////////////////////////////////
int circuit::findtop(node *f, node *g, node *h)
{
	
	if( var_ord[f->var] <= var_ord[g-> var])
	{
		if( var_ord[f-> var] <= var_ord[h-> var])
			return f-> var;
		return h-> var;	
	}
	else
	{
		if( var_ord[g-> var] <= var_ord[h-> var])
			return g-> var;
		return h-> var;	
	}	
}

////////////////////////////////////////////////////////////////////////
// To help with 'IF' co-factor
////////////////////////////////////////////////////////////////////////
node* circuit :: if_cofactor(int topvar, node *f)
{
	if( topvar == f->var)
	{
		node *temp = f->high;
		if( f-> sign == true )
		{
			if(temp == one)
				return zero;
			else if(temp == zero)
				return one;
			else
				return NOT(temp);
		}
		return temp;		
	}
	return f;
}

////////////////////////////////////////////////////////////////////////
// To help with 'ELSE' co-factor
////////////////////////////////////////////////////////////////////////
node* circuit :: else_cofactor(int topvar, node *f)
{
	if( topvar == f->var)
	{
		node *temp = f->low;
		if( f-> sign == true )
		{
			if(temp == one)
				return zero;
			else if(temp == zero)
				return one;
			else
				return NOT(temp);
		}
		return temp;		
	}
	return f;
}

////////////////////////////////////////////////////////////////////////
//The ITE BLOCK
////////////////////////////////////////////////////////////////////////
node* circuit :: ite(node *f, node *g, node *h)
{
	node *tmp;
	
	tmp = CT->find(f,g,h);							// To see if the cache has it
	if( tmp != NULL)
		return tmp;

	//Equivalent Cases	
	if( f == g )
		g = one;
	if( f == h )
		h = zero;	
	if( f->high == g->high && f->low==g->low && f->sign == !g->sign && f->var == g->var )	// If F = G'
		g = zero;
	if( f->high == h->high && f->low==h->low && f->sign == !h->sign && f->var == h->var )	// If F = H'
		h = one;
	
	// Terminal Cases
	if( f == one )
		return g;
	if( f == zero )
		return h;
	if( g == one && h == zero )
		return f;
	if( g == zero && h == one )
		return NOT(f);	
	if( g == h )
		return g;	
	
	int top_var = findtop(f,g,h);			// To find the top variable
	
	node *f1, *f2, *g1, *g2, *h1, *h2;		// Co-factored nodes
	
	f1 = if_cofactor(top_var,f);
	f2 = else_cofactor(top_var,f);
	g1 = if_cofactor(top_var,g);
	g2 = else_cofactor(top_var,g);
	h1 = if_cofactor(top_var,h);
	h2 = else_cofactor(top_var,h);
	
	node *tmp1,*tmp2;
		
	tmp1 = ite(f1,g1,h1);
	tmp2 = ite(f2,g2,h2);
	
	if( tmp1 == tmp2 )
		return tmp1;
		
	tmp = (HT->findoradd(top_var, false, tmp1, tmp2));				// Finds or adds the node
	CT ->insert(f,g,h,tmp);											// Insert into computed table

	return tmp;
}

//////////////////////////////////////////////////////////////////
// Simulates the BDD given input vectors
//////////////////////////////////////////////////////////////////
void circuit :: bddsim(int vecWidth)
{
	int inv;
	node *temp;
	for(int i=0; i<numOutputs; i++)
	{	
		inv = 0;
		temp = nodes[outputs[i]];
		while((temp->index!=1) && (temp->index!=2))
		{
				if(temp->sign==true)
						++inv;
				if( int(vector[(temp->var)-1]) == 49)
					temp = temp -> high;
				else if( int(vector[(temp->var)-1]) == 48)
					temp = temp -> low;	
		}
		if((inv % 2) == 1)
		{
			if(temp == one)
				cout<<'0';
			else if(temp == zero)
				cout<<'1';
		}
		else if((inv % 2) == 0)
		{
			if(temp == one)
				cout<<'1';
			else if(temp == zero)
				cout<<'0';
		}
	}
	cout<<endl;
}

////////////////////////////////////////////////////////////////
// Function to get Vector from File
///////////////////////////////////////////////////////////////

int getVector(FILE *inFile, int vecSize)
{
    int i;
    char thisChar;

    fscanf(inFile, "%c", &thisChar);
    while ((thisChar == SPACE) || (thisChar == RETURN))
	fscanf(inFile, "%c", &thisChar);

    vector[0] = thisChar;
    if (vector[0] == 'E')
	return (0);

    for (i=1; i<vecSize; i++)
	fscanf(inFile, "%c", &vector[i]);
    vector[i] = EOS;

    fscanf(inFile, "%c", &thisChar);
	
    // read till end of line
    while ((thisChar != RETURN) && (thisChar != EOF))
    	fscanf(inFile, "%c", &thisChar);

    return(1);
}

//////////////////////////////////////////////////////////////////
// Get's vectors from getVector and feeds it to
// bddsim for simulation vector by vector
///////////////////////////////////////////////////////////////////
void circuit :: logicSimFromFile(FILE *vecFile, int vecWidth)
{
	int moreVec;
    moreVec = 1;
    
	/*for(int i=0; i<numOutputs; i++)
	{	
		cout<<outputs[i]<<' ';
		temp = nodes[outputs[i]];
		if(nodes[outputs[i]]->sign== true)
			cout<<'-';
		cout<<temp->index<<endl;
	}*/

    while (moreVec)
    {    	
		moreVec = getVector(vecFile, vecWidth);
		if (moreVec == 1)
		{
			printf("vector : %s\n", vector);
			bddsim(vecWidth);        	
		}  // if (moreVec == 1)
    }   // while (getVector...)
}

////////////////////////////////////////////////////////////////////////
// To print the stuff onto a txt file
///////////////////////////////////////////////////////////////////////
void circuit::bddprint(FILE *bdd)
{
	++indices;
	int i;
	node *temp;
	
	//fprintf(bdd, "%d %d %d\n", numInputs, numOutputs, indices);
	
	for (i=0;i<numInputs;i++)
	fprintf(bdd, "%d ", i+1);
	
	fprintf(bdd, "%\n");
	
	for( i=0; i<numOutputs; i++)
	{	
		temp = nodes[outputs[i]];
		if(nodes[outputs[i]]->sign== true)
			fprintf(bdd, "-");
		fprintf(bdd, "%d ", temp->index);
	}
	
	fprintf(bdd, "%\n");
	fprintf(bdd, "1 15 0 0\n");
	fprintf(bdd, "2 15 0 0\n");
	for (i=3;i<indices;i++)
	{	
		//temp = allnodes[i];
		//cout<<temp<<endl;
		//cout<<temp->index<<endl;
		fprintf(bdd, "%d %d ", temp->index, temp->var);
		if ( temp->high->sign == true)						
		{
	        fprintf(bdd, "-");
		}
		fprintf(bdd, "%d ",temp->high->index);
		if ( temp->low->sign == true)						
		{
	        fprintf(bdd, "-");
		}
		fprintf(bdd, "%d \n",temp->low->index);
	}
}

////////////////////////////////////////////////////////////////////////
// Function to count the nodes per BDD (DFS search)
////////////////////////////////////////////////////////////////////////
void circuit::count(node* temp)
{
	++numNodes;
	temp -> visited = true;
	//cout<<temp->index<<'x'<<numNodes<<endl;
	if((temp->high->visited == false) && temp->high!=one && temp->high!=zero)
		count(temp -> high);
	if((temp->low->visited == false) && temp->low!=one && temp->low!=zero)
		count(temp -> low);	
}

////////////////////////////////////////////////////////////////////////
// Function to reset the count 
////////////////////////////////////////////////////////////////////////
void circuit::resetcount(node* temp)
{
	temp -> visited = false;
	if( (temp->high->visited == true) && temp->high!=one && temp->high!=zero )
		resetcount(temp -> high);
	
	if( (temp->low->visited == true) && temp->low!=one && temp->low!=zero )
		resetcount(temp -> low);		
}

////////////////////////////////////////////////////////////////////////
// main starts here
////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
	FILE *bddFile;
	int NUM_THREADS, RUNS_PER_THREAD, k;
		
	if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <ckt> <No. of Threads> <Runs per thread>\n", argv[0]);
        exit(-1);
    }

    strcpy(cktName, argv[1]);
    strcpy(vecName, argv[1]);
	strcpy(bddName, argv[1]);
	strcat(bddName, ".bdd");
	strcat(vecName, ".vec");	

	/* Both the tables are shared by all the threads.
	 * A coarse-grain is employed where each lock
	 * locks one bucket of the hash table  
	 */
	CT = new CompTable(SIZE);   // Initialize Cache
	HT = new HashTable(SIZE);   // Initialize hashTablE
    
    NUM_THREADS = atoi(argv[2]);
    RUNS_PER_THREAD = atoi(argv[3]);    
    pthread_t threads[NUM_THREADS];    

    // This initializes the mutex for simulating
    // the BDD.
    int ret = pthread_mutex_init(&mutexsim, NULL);
	if(ret)
	{
		cout<<"Simulation Mutex not created properly!!"<<endl;
		exit(-1);
	}

	// This initializes the mutex for counting
    // the BDD.
    ret = pthread_mutex_init(&mutexcnt, NULL);
	if(ret)
	{
		cout<<"Simulation Mutex not created properly!!"<<endl;
		exit(-1);
	}

	// This creates the threads
    for(k=0; k<NUM_THREADS; k++)
    {
    	int ret = pthread_create( &threads[k], NULL, RUNBDD, (void *) &RUNS_PER_THREAD); 
    	
    	// Checking for thread failure 
		if (ret)
		{
			cout<<"Thread(s) not created!!!";
			exit(-1);
		}
    }

    // Run the threads
    for (k = 0; k < NUM_THREADS; ++k)
    {
    	pthread_join(threads[k], NULL);
    }	 

	// Termination of threads
	for (k = 0; k < NUM_THREADS; ++k)
    {
    	pthread_exit(&threads[k]);
    }

	delete CT;
	delete HT;	

	/*bddFile = fopen(bddName, "w");
	if (bddFile == NULL)
    {
	fprintf(stderr, "Can't open %s\n", bddName);
	exit(-1);
    }	
    
    /*ckt = new circuit(cktName); 
    //t1=clock();
	ckt -> bdd();
	if (vecFile != NULL)
	ckt -> logicSimFromFile(vecFile, vecWidth);
	//t2=clock();
	//cout<<"\n Total Time (in secs) :";
	//float diff = ((float)t2-(float)t1);
    //cout<<diff/CLOCKS_PER_SEC<<endl;
	cout<<" Total No. of Nodes :"<<indices;
	//ckt -> bddprint(bddFile);    */
}

////////////////////////////////////////////////////////////////////////
// Each thread runs this function parallely
// This runs the bdd generation 
////////////////////////////////////////////////////////////////////////
void *RUNBDD(void* ptr)
{
	int x  = *((int *)ptr);
	pthread_mutex_lock (&mutexsim);
	int localWindow = globalWindow++;
	pthread_mutex_unlock (&mutexsim);
	for(int j=0; j<x; ++j)
	{
		//allnodes = new node*[6000000];
		clock_t t1,t2;
    	circuit *ckt;							// To initialize the class
		ckt = new circuit(cktName); 
    	t1  = clock();
		ckt -> bdd(localWindow);
		t2  = clock();
		//cout<<"\n Total Time (in secs) :";
		float diff = ((float)t2-(float)t1);
    	//cout<<diff/CLOCKS_PER_SEC<<endl;

    	pthread_mutex_lock (&mutexcnt);
    	for(int i=0; i<ckt->numOutputs; i++)
		{	
			ckt -> count(ckt->nodes[ckt->outputs[i]]);
		}
		for(int i=0; i<ckt->numOutputs; i++)
		{	
			ckt -> resetcount(ckt->nodes[ckt->outputs[i]]);
		}
    	pthread_mutex_unlock (&mutexcnt);

    	ckt->numNodes += 2;

    	pthread_mutex_lock (&mutexsim);
    	vecFile = fopen(vecName, "r");
    	if (vecFile == NULL)
    	{
			fprintf(stderr, "Can't open %s\n", vecName);
			exit(-1);
    	}
		fscanf(vecFile, "%d", &vecWidth);		
		cout<<"shuffle: "<<(localWindow*scramble)+1<<':'<<((localWindow+1)*scramble)<<endl;
		cout<<"Variable Order: ";
		for (int i=1; i<=ckt->numInputs; i++)
			cout<<ckt->var_ord[i]<<' ';
	    cout<<endl;
	    cout<<"Number of Nodes : "<<ckt->numNodes<<endl;

    	//ckt -> logicSimFromFile(vecFile, vecWidth);
    	cout<<endl;
    	pthread_mutex_unlock (&mutexsim);

    	delete ckt;
		//cout<<" Total No. of Nodes :"<<indices<<endl<<endl;	
		//indices = 2;	
		//delete allnodes;
	}
}