#pragma once

struct PartitionTreeNode
{
	float width;
	float height;
	float x;
	float y;

	bool               used   = false;
	PartitionTreeNode* parent = nullptr;
	PartitionTreeNode* left   = nullptr;
	PartitionTreeNode* right  = nullptr;
};

struct PartitionTree
{
	float width;
	float height;

	PartitionTreeNode root;
};

PartitionTreeNode* PartitionTree_AddNode( PartitionTree* tree, float width, float height );
void               PartitionTree_RemoveNode( PartitionTree* tree, PartitionTreeNode* node );
