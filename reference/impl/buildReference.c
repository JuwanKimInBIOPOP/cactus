#include "cactus.h"
#include "sonLib.h"
#include "cycleConstrainedMatchingAlgorithms.h"

static stList *getExtraAttachedStubsFromParent(Flower *flower) {
    /*
     * Copy any stubs not present in the flower from the parent group.
     * At the end there will be an even number of attached stubs in the problem.
     * Returns the list of new ends, which need to be assigned a group.
     */
    Group *parentGroup = flower_getParentGroup(flower);
    stList *newEnds = stList_construct();
    if (parentGroup != NULL) {
        Group_EndIterator *parentEndIt = group_getEndIterator(parentGroup);
        End *parentEnd;
        while ((parentEnd = group_getNextEnd(parentEndIt)) != NULL) {
            if (end_isAttached(parentEnd) || end_isBlockEnd(parentEnd)) {
                End *end = flower_getEnd(flower, end_getName(parentEnd));
                if (end == NULL) { //We have found an end that needs to be pushed into the child.
                    stList_append(newEnds, end_copyConstruct(parentEnd, flower)); //At this point it has no associated group;
                }
            }
        }
        group_destructEndIterator(parentEndIt);
    }
    assert(flower_getAttachedStubEndNumber(flower) % 2 == 0);
    assert(flower_getAttachedStubEndNumber(flower) > 0);
    return newEnds;
}

static void iterateOverTangleEnds(Flower *flower,
        void(*fn)(End *end, void *extraArg), void *extraArg) {
    /*
     * Iterates over all ends in tangle groups of the flower and passes the given
     * end to fn.
     */
    Flower_GroupIterator *groupIt = flower_getGroupIterator(flower);
    Group *group;
    while ((group = flower_getNextGroup(groupIt)) != NULL) {
        if (group_isTangle(group)) {
            Group_EndIterator *endIt = group_getEndIterator(group);
            End *end;
            while ((end = group_getNextEnd(endIt)) != NULL) {
                fn(end, extraArg);
            }
            group_destructEndIterator(endIt);
        }
    }
    flower_destructGroupIterator(groupIt);
}

static void getMapOfTangleEndsToIntegersP(End *end, void **extraArgs) {
    stHash *endsToNodes = extraArgs[0];
    int32_t *endCount = extraArgs[1];
    stHash_insert(endsToNodes, end, stIntTuple_construct(1, (*endCount)++));
}

static stHash *getMapOfTangleEndsToNodes(Flower *flower) {
    /*
     * Iterates over the tangle ends in the problem and places them in a hash, each mapping to a unique integer 'node'.
     */
    stHash *endsToNodes = stHash_construct2(NULL,
            (void(*)(void *)) stIntTuple_destruct);
    int32_t endCount = 0;
    void *args[] = { endsToNodes, &endCount };
    iterateOverTangleEnds(flower,
            (void(*)(End *, void *)) getMapOfTangleEndsToIntegersP, args);
    return endsToNodes;
}

static stHash *getMapOfNodesToTangleEnds(stHash *endsToNodes) {
    /*
     * Inverts the ends to nodes hash.
     */
    stHash *nodesToEnds = stHash_construct3(
            (uint32_t(*)(const void *)) stIntTuple_hashKey,
            (int(*)(const void *, const void *)) stIntTuple_cmpFn, NULL, NULL);
    stHashIterator *hashIt = stHash_getIterator(endsToNodes);
    End *end;
    while ((end = stHash_getNext(hashIt)) != NULL) {
        stHash_insert(nodesToEnds, stHash_search(endsToNodes, end), end);
    }
    stHash_destructIterator(hashIt);
    return nodesToEnds;
}

static Cap *getCapWithEvent(End *end, Event *event) {
    /*
     * Get the first encountered cap in the end with the given event.
     */
    Cap *cap;
    End_InstanceIterator *instanceIt = end_getInstanceIterator(end);
    while ((cap = end_getNext(instanceIt)) != NULL) {
        if (event_getName(cap_getEvent(cap)) == event_getName(event)) {
            end_destructInstanceIterator(instanceIt);
            return cap;
        }
    }
    end_destructInstanceIterator(instanceIt);
    return NULL;
}

static End *getAdjacentEndFromParent(End *end, Event *referenceEvent) {
    /*
     * Get the adjacent stub end by looking at the reference adjacency in the parent.
     */
    Flower *flower = end_getFlower(end);
    Group *parentGroup = flower_getParentGroup(flower);
    assert(parentGroup != NULL);
    End *parentEnd = group_getEnd(parentGroup, end_getName(end));
    assert(parentEnd != NULL);
    //Now trace the parent end..
    Cap *cap = getCapWithEvent(end, referenceEvent);
    assert(cap != NULL);
    Cap *adjacentCap = cap_getAdjacency(cap);
    assert(adjacentCap != NULL);
    End *adjacentParentEnd = cap_getEnd(adjacentCap);
    End *adjacentEnd = flower_getEnd(flower, end_getName(adjacentParentEnd));
    assert(adjacentEnd != NULL);
    return adjacentEnd;
}

static stIntTuple *getEdge2(int32_t node1, int32_t node2) {
    return node1 < node2 ? stIntTuple_construct(2, node1, node2)
            : stIntTuple_construct(2, node2, node1);
}

static stIntTuple *getEdge(End *end1, End *end2, stHash *endsToNodes) {
    return getEdge2(
            stIntTuple_getPosition(stHash_search(endsToNodes, end1), 0),
            stIntTuple_getPosition(stHash_search(endsToNodes, end2), 0));
}

static stList *getStubEdgesFromParent(Flower *flower, stHash *endsToNodes,
        Event *referenceEvent) {
    /*
     * For each attached stub in the flower, get the end in the parent group, and add its
     * adjacency to the group.
     */
    stList *stubEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    Flower_EndIterator *endIt = flower_getEndIterator(flower);
    End *end = NULL;
    stSortedSet *endsSeen = stSortedSet_construct();
    while ((end = flower_getNextEnd(endIt)) != NULL) {
        if (end_isAttached(end) && end_isStubEnd(end) && stSortedSet_search(
                endsSeen, end) == NULL) {
            End *adjacentEnd = getAdjacentEndFromParent(end, referenceEvent);

            //Do lots of checks!
            assert(end_getGroup(end) != NULL);
            assert(group_isTangle(end_getGroup(end)));
            assert(adjacentEnd != NULL);
            assert(end_getGroup(adjacentEnd) != NULL);
            assert(group_isTangle(end_getGroup(adjacentEnd)));
            assert(stSortedSet_search(endsSeen, adjacentEnd) == NULL);
            assert(adjacentEnd != end);

            stSortedSet_insert(endsSeen, end);
            stSortedSet_insert(endsSeen, adjacentEnd);
            stList_append(stubEdges, getEdge(end, adjacentEnd, endsToNodes));
        }
    }
    flower_destructEndIterator(endIt);
    stSortedSet_destruct(endsSeen);
    return stubEdges;
}

static void getArbitraryStubEdgesP(stSortedSet *nodesSet, int32_t node) {
    stIntTuple *i = stIntTuple_construct(1, node);
    if (stSortedSet_search(nodesSet, i) != NULL) {
        stSortedSet_remove(nodesSet, i);
    }
    stIntTuple_destruct(i);
}

static stList *getArbitraryStubEdges(stHash *endsToNodes, stList *chainEdges) {
    /*
     * Make a set of stub edges, pairing the 'stub ends' arbitrarily.
     */
    stList *nodes = stHash_getValues(endsToNodes);
    stSortedSet *nodesSet = stList_getSortedSet(nodes,
            (int(*)(const void *, const void *)) stIntTuple_cmpFn);
    stList_destruct(nodes);

    /*
     * Filter the set of nodes for those that are part of chain edges.
     */
    for (int32_t i = 0; i < stList_length(chainEdges); i++) {
        stIntTuple *edge = stList_get(chainEdges, i);
        getArbitraryStubEdgesP(nodesSet, stIntTuple_getPosition(edge, 0));
        getArbitraryStubEdgesP(nodesSet, stIntTuple_getPosition(edge, 1));
    }

    assert(stSortedSet_size(nodesSet) > 0);
    assert(stSortedSet_size(nodesSet) % 2 == 0);

    /*
     * The remaining set of edges are 'stubs', create an arbitrary pairing and return it.
     */
    nodes = stSortedSet_getList(nodesSet);
    stList *stubEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    for (int32_t i = 0; i < stList_length(nodes); i += 2) {
        stList_append(
                stubEdges,
                getEdge2(stIntTuple_getPosition(stList_get(nodes, i), 0),
                        stIntTuple_getPosition(stList_get(nodes, i + 1), 0)));
    }

    stSortedSet_destruct(nodesSet);
    stList_destruct(nodes);

    return stubEdges;
}

static void getNonTrivialChainEdges(Flower *flower, stHash *endsToNodes,
        stList *chainEdges) {
    /*
     * Get the non-trivial chain edges for the flower.
     */
    Flower_ChainIterator *chainIt = flower_getChainIterator(flower);
    Chain *chain;
    while ((chain = flower_getNextChain(chainIt)) != NULL) {
        End *_5End = link_get5End(chain_getFirst(chain));
        End *_3End = link_get3End(chain_getLast(chain));
        if (end_isBlockEnd(_5End) && end_isBlockEnd(_3End)) {
            End *end1 = end_getOtherBlockEnd(_5End);
            End *end2 = end_getOtherBlockEnd(_3End);

            assert(stHash_search(endsToNodes, end1) != NULL);
            assert(stHash_search(endsToNodes, end2) != NULL);

            stList_append(chainEdges, getEdge(end1, end2, endsToNodes));
        }
    }
    flower_destructChainIterator(chainIt);
}

static void getTrivialChainEdges(Flower *flower, stHash *endsToNodes,
        stList *chainEdges) {
    /*
     * Get the trivial chain edges for the flower.
     */
    Flower_BlockIterator *blockIt = flower_getBlockIterator(flower);
    Block *block;
    while ((block = flower_getNextBlock(blockIt)) != NULL) {
        End *_5End = block_get5End(block);
        End *_3End = block_get3End(block);
        if (group_isTangle(end_getGroup(_5End))) {
            assert(group_isTangle(end_getGroup(_3End)));
            stList_append(chainEdges, getEdge(_5End, _3End, endsToNodes));
        } else {
            assert(group_isLink(end_getGroup(_3End)));
        }
    }
    flower_destructBlockIterator(blockIt);
}

static stList *getChainEdges(Flower *flower, stHash *endsToNodes) {
    /*
     * Get the trivial chain edges for the flower.
     */
    stList *chainEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    getNonTrivialChainEdges(flower, endsToNodes, chainEdges);
    getTrivialChainEdges(flower, endsToNodes, chainEdges);
    return chainEdges;
}

static void getAdjacencyEdgesP(End *end, void **args) {
    /*
     * This function adds adjacencies
     */
    stHash *endToInts = args[0];
    stList *adjacencyEdges = args[1];
    End_InstanceIterator *instanceIt = end_getInstanceIterator(end);
    Cap *cap;
    while ((cap = end_getNext(instanceIt)) != NULL) {
        cap = cap_getStrand(cap) ? cap : cap_getReverse(cap);
        if (cap_getSide(cap)) { //Use this side property to avoid adding edges twice.
            stList_append(adjacencyEdges,
                    getEdge(end, cap_getEnd(cap_getAdjacency(cap)), endToInts));
        }
    }
    end_destructInstanceIterator(instanceIt);
}

static stList *getAdjacencyEdges(Flower *flower, stHash *endsToNodes) {
    /*
     * Get the adjacency edges for the flower.
     */

    /*
     * First build a list of adjacencies from the tangle ends.
     */
    stList *adjacencyEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);
    void *args[] = { endsToNodes, adjacencyEdges };
    iterateOverTangleEnds(flower, (void(*)(End *, void *)) getAdjacencyEdgesP,
            args);

    /*
     * Sort them in ascending order.
     */
    stList_sort(adjacencyEdges,
            (int(*)(const void *, const void *)) stIntTuple_cmpFn);

    /*
     * Iterate over the list, building weighted edges according to the number of duplicates.
     */
    stList *weightedAdjacencyEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);

    int32_t weight = 0;
    stIntTuple *edge = NULL;
    while (stList_length(adjacencyEdges) > 0) {
        stIntTuple *edge2 = stList_pop(adjacencyEdges);
        if (edge != NULL && stIntTuple_cmpFn(edge, edge2) == 0) {
            weight++;
        } else {
            if (edge != NULL) {
                stList_append(
                        weightedAdjacencyEdges,
                        stIntTuple_construct(3,
                                stIntTuple_getPosition(edge, 0),
                                stIntTuple_getPosition(edge, 1), weight));
            }
            edge = edge2;
            weight = 1;
        }
    }
    if (edge != NULL) {
        stList_append(
                weightedAdjacencyEdges,
                stIntTuple_construct(3, stIntTuple_getPosition(edge, 0),
                        stIntTuple_getPosition(edge, 1), weight));
    }

    return adjacencyEdges;
}

End *getEndFromNode(stHash *nodesToEnds, int32_t node) {
    /*
     * Get the end for the given node.
     */
    stIntTuple *i = stIntTuple_construct(1, node);
    End *end = stHash_search(nodesToEnds, i);
    assert(end != NULL);
    stIntTuple_destruct(i);
    return end;
}

static void addBridgeBlocks(Flower *flower, stList *chosenAdjacencyEdges,
        stHash *nodesToEnds) {
    /*
     * For edge adjacency edge that bridges between to groups add a block with an end in each.
     */
    for (int32_t i = 0; i < stList_length(chosenAdjacencyEdges); i++) {
        stIntTuple *edge = stList_get(chosenAdjacencyEdges, i);
        End *end1 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 0));
        End *end2 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 1));
        if (end_getGroup(end1) != end_getGroup(end2)) { //We need to add a bridge block.
            Block *block = block_construct(1, flower);
            end_setGroup(block_get5End(block), end_getGroup(end1));
            end_setGroup(block_get3End(block), end_getGroup(end2));
        }
    }
}

static Cap *makeCapWithEvent(End *end, Event *referenceEvent) {
    /*
     * Returns a cap in the end that is part of the given reference event.
     */
    Cap *cap = getCapWithEvent(end, referenceEvent);
    if (cap == NULL) {
        if (end_isBlockEnd(end)) {
            Block *block = end_getBlock(end);
            segment_construct(block, referenceEvent);
            cap = getCapWithEvent(end, referenceEvent);
        } else {
            cap = cap_construct(end, referenceEvent);
        }
    }
    assert(cap != NULL);
    return cap;
}

static void addAdjacenciesAndSegments(Flower *flower,
        stList *chosenAdjacencyEdges, stHash *nodesToEnds,
        Event *referenceEvent) {
    /*
     * For each chosen adjacency edge ensure there exists a cap in each incident end (adding segments as needed),
     * and a node.
     */
    for (int32_t i = 0; i < stList_length(chosenAdjacencyEdges); i++) {
        stIntTuple *edge = stList_get(chosenAdjacencyEdges, i);
        End *end1 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 0));
        End *end2 =
                getEndFromNode(nodesToEnds, stIntTuple_getPosition(edge, 1));
        Cap *cap1 = makeCapWithEvent(end1, referenceEvent);
        Cap *cap2 = makeCapWithEvent(end2, referenceEvent);
        assert(cap_getAdjacency(cap1) == NULL);
        assert(cap_getAdjacency(cap2) == NULL);
        cap_makeAdjacent(cap1, cap2);
    }
}

static stList *getLinkEdges(Flower *flower, stHash *endsToNodes) {
    /*
     * Get edges that represent all the links.
     */
    stList *linkEdges = stList_construct3(0,
            (void(*)(void *)) stIntTuple_destruct);

    Flower_GroupIterator *groupIt = flower_getGroupIterator(flower);
    Group *group;
    while ((group = flower_getNextGroup(groupIt)) != NULL) {
        if (group_isLink(group)) {
            Link *link = group_getLink(group);
            stList_append(
                    linkEdges,
                    getEdge(link_get5End(link), link_get3End(link), endsToNodes));
        }
    }
    flower_destructGroupIterator(groupIt);

    return linkEdges;
}

static void assignGroups(stList *newEnds) {
    /*
     * Put ends into groups.
     */
    for (int32_t i = 0; i < stList_length(newEnds); i++) {
        End *end = stList_get(newEnds, i);
        assert(end_getInstanceNumber(end) == 1);
        Cap *cap = end_getFirst(end);
        Cap *adjacentCap = cap_getAdjacency(cap);
        assert(adjacentCap != NULL);
        End *adjacentEnd = cap_getEnd(adjacentCap);
        Group *group = end_getGroup(adjacentEnd);
        assert(group != NULL);
        end_setGroup(end, group);
    }
}

static Event *getEventByHeader(EventTree *eventTree, const char *eventHeader) {
    /*
     * Finds an event in the given event tree with the given header string.
     */
    EventTree_Iterator *it = eventTree_getIterator(eventTree);
    Event *event;
    while ((event = eventTree_getNext(it)) != NULL) {
        if (strcmp(event_getHeader(event), eventHeader) == 0) {
            eventTree_destructIterator(it);
            return event;
        }
    }
    eventTree_destructIterator(it);
    return NULL;
}

static Event *getReferenceEvent(Flower *flower,
        const char *referenceEventHeader) {
    /*
     * Create the reference event, with the given header, for the flower.
     */
    EventTree *eventTree = flower_getEventTree(flower);
    Event *referenceEvent = getEventByHeader(eventTree, referenceEventHeader);
    if (referenceEvent == NULL) {
        Group *parentGroup = flower_getParentGroup(flower);
        if (parentGroup == NULL) {
            //We are the root, so make a new event..
            return event_construct3(referenceEventHeader, INT32_MAX,
                    eventTree_getRootEvent(eventTree), eventTree);
        } else {
            Event *parentEvent = getEventByHeader(
                    flower_getEventTree(group_getFlower(parentGroup)),
                    referenceEventHeader);
            assert(parentEvent != NULL);
            return event_construct(event_getName(parentEvent),
                    referenceEventHeader, INT32_MAX,
                    eventTree_getRootEvent(eventTree), eventTree);
        }
    }
    return referenceEvent;
}

static void addLinkBreakingBlocks(Flower *flower) {
    /*
     * Adds two block ends to groups with just one end in them, to avoid
     * creating new links when creating the reference.
     */
    Flower_GroupIterator *groupIt = flower_getGroupIterator(flower);
    Group *group;
    while ((group = flower_getNextGroup(groupIt)) != NULL) {
        if (group_isTangle(group) && group_getEndNumber(group) == 1) { //If we don't add an extra block to the group we're going to be in trouble!
            Block *block = block_construct(1, flower);
            end_setGroup(block_get5End(block), group);
            end_setGroup(block_get3End(block), group);
        }
    }
    flower_destructGroupIterator(groupIt);
}

void buildReferenceTopDown(Flower *flower, const char *referenceEventHeader,
        stList *(*matchingAlgorithm)(stList *edges, int32_t nodeNumber)) {
    /*
     * Get the reference event
     */
    Event *referenceEvent = getReferenceEvent(flower, referenceEventHeader);

    /*
     * Get any extra ends to balance the group from the parent problem.
     */
    stList *newEnds = getExtraAttachedStubsFromParent(flower);

    /*
     * Add extra link breaking block (hack!)
     */
    addLinkBreakingBlocks(flower);

    /*
     * Create a map to identify the ends by integers.
     */
    stHash *endsToNodes = getMapOfTangleEndsToNodes(flower);
    stHash *nodesToEnds = getMapOfNodesToTangleEnds(endsToNodes);
    int32_t nodeNumber = stHash_size(endsToNodes);
    assert(nodeNumber % 2 == 0);
    assert(nodeNumber > 0);

    /*
     * Get the chain edges.
     */
    stList *chainEdges = getChainEdges(flower, endsToNodes);

    /*
     * Get the stub edges.
     */
    bool hasParent = flower_getParentGroup(flower) != NULL;
    stList *stubEdges = hasParent ? getStubEdgesFromParent(flower, endsToNodes,
            referenceEvent) : getArbitraryStubEdges(endsToNodes, chainEdges);

    /*
     * Get the adjacency edges.
     */
    stList *adjacencyEdges = getAdjacencyEdges(flower, endsToNodes);

    /*
     * Calculate the matching
     */
    stList *chosenAdjacencyEdges = chooseMatching(nodeNumber, adjacencyEdges,
            stubEdges, chainEdges, !hasParent, matchingAlgorithm);

    /*
     * Add in any extra blocks to bridge between groups.
     */
    addBridgeBlocks(flower, chosenAdjacencyEdges, nodesToEnds);

    /*
     * Get link adjacencies.
     */
    stList *linkEdges = getLinkEdges(flower, endsToNodes);
    stList_appendAll(chosenAdjacencyEdges, linkEdges);

    /*
     * Add the reference genome into flower
     */
    addAdjacenciesAndSegments(flower, chosenAdjacencyEdges, nodesToEnds,
            referenceEvent);

    /*
     * Ensure the newly created ends have a group.
     */
    assignGroups(newEnds);

    /*
     * Cleanup
     */
    stList_destruct(newEnds);
    stHash_destruct(endsToNodes);
    stHash_destruct(nodesToEnds);
    stList_destruct(chainEdges);
    stList_destruct(stubEdges);
    stList_destruct(adjacencyEdges);
    stList_destruct(linkEdges);
    stList_destruct(chosenAdjacencyEdges);

    /*
     * Check
     */
#ifdef BEN_DEBUG
    flower_check(flower);
#endif
}

