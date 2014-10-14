//////////////////////////////////////////////////////////////////////
// ROBDD Implementation - Static Variable Ordering
// Kaushik Rangarajan
/////////////////////////////////////////////////////////////////////
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ctime>
#include <pthread.h>
#include <atomic>

using namespace std;

//////////////////////////////////////////////////////////////////////
// Hash Table Shtuff
//////////////////////////////////////////////////////////////////////
std::atomic<unsigned long> indices(2);	// Index value of node
#define SIZE 100000
#define hash(a,b,c) ((a+b+c)%SIZE)

// Each node of a BDD
class node
{
	public :
	int index;							// Unique ID number of node
	unsigned short var;					// Variable Number of Node
	bool sign;							// To represent complemented edges
	class node *low;					// Pointer to the zero-child
	class node *high;					// Pointer to the one-child
	class node *next;					// Pointer to next node in the seperate chaining hash tree
	bool visited;						// Helps in counting no. of nodes per BDD (DFS search)
		
	node()								// Constructor for Node
	{
		index = var = 0;
		sign = visited = false;
		low = high = next = NULL;
	}
};

//////////////////////////////////////////////////////////////////
// Terminal Nodes 
node *one = new node();										  // Node for terminal-one
node *zero = new node();									  // Node for terminal-zero
node **allnodes;											  // To keep track of all nodes to later print in file

//////////////////////////////////////////////////////////////////

// Each list of a Hash Table
class list
{
	public :
	node *head;
	list()														  // Constructor Creates Empty List
	{
		head = NULL;
	}
	node *add(int data, bool bit, node *high, node *low)					  // Appends data
	{  
		if(head == NULL)
		{
		head = new node();
		head->var = data;
		head->high = high;
		head->low = low;
		head->sign = bit;
		head->next = NULL;
		head->index = ++indices;
		//allnodes[indices] = head;
		return  head;
		}
		else
		{
		node *temp = new node();
		temp -> var = data;
		temp -> high = high;
		temp -> low = low;
		temp -> sign = bit;
		temp -> next = NULL;		
		temp -> index = ++indices;
		node *curr = new node();
		curr = head;
		    
		while(curr!=NULL && curr->next!=NULL)
		{
			curr = curr -> next;
		}
		curr -> next = temp;
		
		//allnodes[indices] = temp;
		return temp;
		}
	}
	
	node *find(int data, bool bit, node *high, node *low)					// Checks if that node is already present
	{
		if(head == NULL)
		{
			return NULL;
		}
		else
		{
			node *curr;
			curr = head;
    
			while(curr != NULL)
			{
				if(curr->var == data && curr->sign == bit && curr-> high == high && curr -> low == low )		
					return curr;
				curr = curr -> next;
			}
			return NULL;
		}
	}
	
	void print()														  // Prints the current list
	{
		node *curr;
		curr = head;
		while( curr != NULL )
		{
			if(curr->sign == true)
				cout<<'-';
			cout<<curr->index<<' '<<curr->var<<' ';
			if(curr->high->sign == true)
				cout<<'-';
			cout<<curr->high->index<<' ';
			if(curr->low->sign == true)
				cout<<'-';
			cout<<curr->low->index<<';';
			curr = curr -> next;
		}
		cout<<endl;
	}
};
				
// The Hash Table
class HashTable
{
	public :
	int tsize;
	list **lists;												  				// Array of nodes
	/* Mutex for every bucket of the hash table. 
	 * A coarse grain-lock.
	 */
	pthread_mutex_t *mutexht;					
	HashTable(int size)
	{
		tsize = size;	
		lists = new list *[tsize];
		mutexht = new pthread_mutex_t[tsize];

		for (int i = 0; i < tsize; i ++)
		{
			lists[i] = new list();
			int ret = pthread_mutex_init(&mutexht[i], NULL);

			// Checks if the mutex is created properly
			if(ret)
			{
				cout<<"Mutex not created properly in HT!!"<<endl;
				exit(-1);
			}
		}
	}
	
	node *findoradd(int x, bool bit, node *high, node *low)						// Finds or Appends new data 
	{
		int v = hash(x,high->index,low->index);
	    node *curr;
	    pthread_mutex_lock (&mutexht[v]);
		curr = lists[v]->find(x, bit, high, low);
		if(curr == NULL)
			curr = lists[v] -> add(x, bit, high, low);		
		pthread_mutex_unlock (&mutexht[v]);
		return (curr);
	}	
	
	void printtable()															// Prints the HastTable
	{
		for (int i = 0; i < tsize; i ++)
		{
			cout<<"Bucket "<<i<<" : ";
			lists[i]->print();
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////
// One Block of the Computed Table 
///////////////////////////////////////////////////////////////////////////////////////////
class cache
{
public :
	node *f;
	node *g;
	node *h;
	node *result;
	cache()
	{
		f = g = h = result = NULL;
	}
};

///////////////////////////////////////////////////////////////////////////////////////////
// The Computed Table
//////////////////////////////////////////////////////////////////////////////////////////
class CompTable
{
public :
	cache **blocks;
	int ctsize;

	/* Mutex for every bucket of the hash table. 
	 * A coarse grain-lock.
	 */	
	pthread_mutex_t *mutexct;

	CompTable(int size)
	{
		ctsize = size;	
		mutexct = new pthread_mutex_t[ctsize];
		blocks = new cache *[ctsize];
		for (int i = 0; i < ctsize; i ++)
		{
			blocks[i] = new cache();			

			// Checks if every mutex is initialized properly
			int ret = pthread_mutex_init(&mutexct[i], NULL);
			if(ret)
			{
				cout<<"Mutex not created properly in CT!!"<<endl;
				exit(-1);
			}
		}		
	}
	void insert( node *F, node *G, node *H, node *res)
	{
		int v = hash(F->index, G->index, H->index);
		pthread_mutex_lock (&mutexct[v]);
		blocks[v]->f = F;
		blocks[v]->g = G;
		blocks[v]->h = H;
		blocks[v]->result = res;
		pthread_mutex_unlock (&mutexct[v]);
	}
	node *find( node *F, node *G, node *H)
	{
		node* foundNode = NULL;
		int v = hash(F->index, G->index, H->index);
		pthread_mutex_lock (&mutexct[v]);
		
		if( blocks[v]->f == F && blocks[v]->g == G && blocks[v]->h == H )
			foundNode = blocks[v]->result;

		pthread_mutex_unlock (&mutexct[v]);
		return foundNode;
	}
};
