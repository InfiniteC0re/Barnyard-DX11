#include "pch.h"
#include "PartitionTree.h"

//-----------------------------------------------------------------------------
// Enables memory debugging.
// Note: Should be the last include!
//-----------------------------------------------------------------------------
#include <Core/TMemoryDebugOn.h>

TOSHI_NAMESPACE_USING

static void SplitNode( PartitionTree* tree, PartitionTreeNode* node, float width, float height )
{
	TVALIDPTR( node );
	TASSERT( node->height >= height );
	TASSERT( node->width >= width );
	TASSERT( !node->used );

	if ( node->height == height )
	{
		// Split only horizontally
		PartitionTreeNode* hor_node = NULL;

		if ( node->width - width > 0 )
		{
			hor_node         = new PartitionTreeNode();
			hor_node->parent = node;
			hor_node->width  = node->width - width;
			hor_node->height = node->height;
			hor_node->x      = node->x + width;
			hor_node->y      = node->y;
		}

		node->used  = true;
		node->width = width;

		if ( !node->right )
		{
			node->right = hor_node;
		}
		else if ( !node->left )
		{
			node->left = hor_node;
		}
		else
		{
			TASSERT( TFALSE );
			return;
		}
	}
	else if ( node->width == width )
	{
		PartitionTreeNode* ver_node = NULL;

		if ( node->height - height > 0 )
		{
			ver_node         = new PartitionTreeNode();
			ver_node->parent = node;
			ver_node->width  = node->width;
			ver_node->height = node->height - height;
			ver_node->x      = node->x;
			ver_node->y      = node->y + height;
		}

		node->used   = true;
		node->height = height;

		if ( !node->right )
		{
			node->right = ver_node;
		}
		else if ( !node->left )
		{
			node->left = ver_node;
		}
		else
		{
			TASSERT( TFALSE );
			return;
		}
	}
	else
	{
		// Split both horizontally and vertically
		PartitionTreeNode* hor_node = NULL;
		PartitionTreeNode* ver_node = NULL;

		if ( node->width - width > 0 )
		{
			hor_node         = new PartitionTreeNode();
			hor_node->parent = node;
			hor_node->width  = node->width - width;
			hor_node->height = height;
			hor_node->x      = node->x + width;
			hor_node->y      = node->y;
		}

		if ( node->height - height > 0 )
		{
			ver_node         = new PartitionTreeNode();
			ver_node->parent = node;
			ver_node->width  = node->width;
			ver_node->height = node->height - height;
			ver_node->x      = node->x;
			ver_node->y      = node->y + height;
		}

		node->used   = true;
		node->width  = width;
		node->height = height;

		if ( ver_node->width * ver_node->height >= hor_node->width * hor_node->height )
		{
			node->left  = ver_node;
			node->right = hor_node;
		}
		else
		{
			node->left  = hor_node;
			node->right = ver_node;
		}
	}
}

static PartitionTreeNode* TraverseNodeSize( PartitionTreeNode* node, float width, float height )
{
	PartitionTreeNode* split_node = NULL;

	if ( PartitionTreeNode* right = node->right )
	{
		if ( !right->used && right->width >= width && right->height >= height )
			split_node = right;
		else
			split_node = TraverseNodeSize( right, width, height );
	}

	if ( !split_node )
	{
		if ( PartitionTreeNode* left = node->left )
		{
			if ( !left->used && left->width >= width && left->height >= height )
				split_node = left;
			else
				split_node = TraverseNodeSize( left, width, height );
		}
	}

	return split_node;
}

PartitionTreeNode* PartitionTree_AddNode( PartitionTree* tree, float width, float height )
{
	// Traverse nodes to find the one we need
	PartitionTreeNode* node       = &tree->root;
	PartitionTreeNode* split_node = NULL;

	if ( !node->right && !node->left )
		split_node = node;

	if ( !split_node )
		split_node = TraverseNodeSize( node, width, height );

	if ( !split_node ) return NULL;

	SplitNode( tree, split_node, width, height );
	return split_node;
}

static bool MergeNodesRight( PartitionTree* tree, PartitionTreeNode* node )
{
	PartitionTreeNode* end = NULL;
	PartitionTreeNode* it  = node;

	while ( it->right && !it->right->used )
	{
		end = it->right;
		it  = it->right;
	}

	while ( end && end != node )
	{
		if ( end->used )
			__debugbreak();

		PartitionTreeNode* parent = end->parent;
		bool               merge  = false;

		if ( end->x == parent->x && end->width == parent->width )
		{
			parent->height += end->height;
			merge = true;
		}
		else if ( end->y == parent->y && end->height == parent->height )
		{
			parent->width += end->width;
			merge = true;
		}

		if ( merge )
		{
			parent->right = end->right;
			if ( end->right ) end->right->parent = parent;
			if ( end->left ) end->left->parent = parent;
			delete end;

			return true;
		}

		end = parent;
	}

	return false;
}

static bool MergeNodesLeft( PartitionTree* tree, PartitionTreeNode* node )
{
	PartitionTreeNode* end = NULL;
	PartitionTreeNode* it  = node;

	while ( it->left && !it->left->used )
	{
		end = it->left;
		it  = it->left;
	}

	while ( end && end != node )
	{
		if ( end->used )
			__debugbreak();

		PartitionTreeNode* parent = end->parent;
		bool               merge  = false;

		if ( end->x == parent->x && end->width == parent->width )
		{
			parent->height += end->height;
			merge = true;
		}
		else if ( end->y == parent->y && end->height == parent->height )
		{
			parent->width += end->width;
			merge = true;
		}

		if ( merge )
		{
			parent->left = end->left;
			if ( end->right ) end->right->parent = parent;
			if ( end->left ) end->left->parent = parent;
			delete end;

			return true;
		}


		end = parent;
	}

	return false;
}

static bool MergeNodesUp( PartitionTree* tree, PartitionTreeNode* node )
{
	PartitionTreeNode* parent = node->parent;

	PartitionTreeNode* end = NULL;
	PartitionTreeNode* it  = node;

	while ( it->parent && !it->parent->used )
	{
		end = it->parent;
		it  = it->parent;

		MergeNodesRight( tree, end );
		MergeNodesLeft( tree, end );
	}

	return true;
}

void PartitionTree_RemoveNode( PartitionTree* tree, PartitionTreeNode* node )
{
	TVALIDPTR( node );
	TASSERT( node->used );

	node->used = false;
	MergeNodesRight( tree, node );
	MergeNodesLeft( tree, node );
	MergeNodesUp( tree, node );
}
