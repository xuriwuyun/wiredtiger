import networkx as nx

from typing import List, Set

from parse_wt_ast import File, Struct, Function
from build_dependency_graph import AMBIG_NODE
from collections import defaultdict

# Print the links that form the dependency of `from_node` on `to_node`
# This function assumes that the dependency exists
def print_edge(graph, from_node, to_node) -> bool:
    edge = (from_node, to_node)
    link_data = nx.get_edge_attributes(graph, 'link_data')
    link_data[edge].print_type_uses()
    link_data[edge].print_struct_accesses()
    link_data[edge].print_func_calls()

def who_uses(module: str, graph: nx.DiGraph):
    incoming_edges = graph.in_edges(module)

    for edge in sorted(incoming_edges):
        (caller, _) = edge
        print(f"\n{caller}/")
        print_edge(graph, caller, module)

def who_is_used_by(module: str, graph: nx.DiGraph):
    incoming_edges = graph.out_edges(module)

    for edge in sorted(incoming_edges):
        (_, callee) = edge
        print(f"\n{callee}/")
        print_edge(graph, module, callee)

# Given a list of nodes that describe a cycle explain the path to reach it
def explain_cycle(cycle: List[str], graph: nx.DiGraph):

    link_data = nx.get_edge_attributes(graph, 'link_data')
    
    # First check the cycle exists
    for i in range(0, len(cycle)):
        cur_node = cycle[i]
        next_node = cycle[(i + 1) % len(cycle)]
        if not graph.has_edge(cur_node, next_node):
            print(f"No cycle! There is no dependency from '{cur_node}' to '{next_node}'")
            return

    # If it does then dump the path
    for i in range(0, len(cycle)):
        cur_node = cycle[i]
        next_node = cycle[(i + 1) % len(cycle)]
        print(f"From {cur_node} to {next_node}:")
        print_edge(graph, cur_node, next_node)

# Print all functions defined in a module and whether they are private to (accessed only within) the module
def privacy_report_functions(graph, module, all_funcs) -> (int, int):

    incoming_edges = graph.in_edges(module)
    link_data = nx.get_edge_attributes(graph, 'link_data')

    funcs_called_from = defaultdict(set)
    for func in sorted(all_funcs, key=lambda f: f.name):
        for edge in incoming_edges:
            (caller, _) = edge
            if link_data[edge]:
                if link_data[edge].func_calls[func.name]:
                    funcs_called_from[func.name].add(caller)

    print(f"Functions that are private to {module}")
    for func in sorted(all_funcs, key=lambda f: f.name):
        if not funcs_called_from[func.name]:
            print(f"    {func.name}")
    print()

    print(f"Functions that are NOT private to {module}")
    for func in sorted(all_funcs, key=lambda f: f.name):
        if callers := funcs_called_from[func.name]:
            print(f"    {func.name}: {callers}")
    print()

    num_private_funcs = len(all_funcs) - len([f for f in funcs_called_from if funcs_called_from[f]])
    num_total_funcs = len(all_funcs)
    return (num_private_funcs, num_total_funcs)

# Print all structs defined in a module and their struct fields. Report whether the fields are private to (accessed only within) the module
def privacy_report_structs(graph, module, all_structs, ambiguous_fields) -> (int, int):

    incoming_edges = graph.in_edges(module)
    link_data = nx.get_edge_attributes(graph, 'link_data')

    num_private_fields, num_total_fields = 0, 0

    for struct in sorted(all_structs, key=lambda s: s.name):
        print(struct.name)
        for field in sorted(struct.fields):
            if field in ambiguous_fields:
                print(f"    {field}: Ambiguous! Please check manually")
                continue

            num_total_fields += 1
            used_by_modules = set()
            for edge in incoming_edges:
                (caller, _) = edge
                if link_data[edge]:
                    if link_data[edge].struct_accesses[struct.name][field]:
                        used_by_modules.add(caller)

            if len(used_by_modules) == 0:
                print(f"    {field}: Private")
                num_private_fields += 1
            else:
                print(f"    {field}: {sorted(used_by_modules)}")
        print()

    return (num_private_fields, num_total_fields)

import matplotlib.pyplot as plt
def print_graph(module, graph):

    incoming_edges = graph.in_edges(module)
    outgoing_edges = graph.out_edges(module)

    other_nodes = set()

    for (inn, _) in incoming_edges:
        other_nodes.add(inn)

    for (_, out) in outgoing_edges:
        other_nodes.add(out)

    foo = graph.copy()

    for node in graph.nodes():
        if node not in other_nodes and node != module:
            foo.remove_node(node)

    foo.remove_node("Ambiguous linking or parsing failed")
    foo.remove_node("include")
    # Hack
    foo.remove_node("session")
    foo.remove_node("conn")
    foo.remove_node("os_layer")
    foo.remove_node("support")


    for node in foo.nodes():
        for node2 in foo.nodes():
            if module == node or module == node2:
                continue

            # Remove links that don't produce a cycle with module
            if foo.has_edge(node, node2) and not (foo.has_edge(module, node) and foo.has_edge(node2, module)):
                foo.remove_edge(node, node2)
                # print("removed ", node, node2)

            if foo.has_edge(node2, node) and not (foo.has_edge(module, node2) and foo.has_edge(node, module)):
                foo.remove_edge(node2, node)


    # Get all simple cycles in the graph
    all_cycles = list(nx.simple_cycles(foo))

    # Filter cycles that are of length 3 and include the specific node
    example_cycle = []
    for cycle in all_cycles:
        if len(cycle) == 3 and module in cycle:
            example_cycle = cycle
            break

    edge_colors = []
    for u, v in foo.edges():
        if (u in example_cycle and v in example_cycle) and (foo.has_edge(u, v) or foo.has_edge(v, u)):  # Check if the reverse edge exists
            edge_colors.append('red')  # Color for bidirectional edges
        else:
            edge_colors.append('none')

    # Center on node
    pos = nx.circular_layout(foo)

    # Draw the graph
    plt.figure(figsize=(8, 6))
    nx.draw(foo, pos, edge_color=edge_colors, with_labels=True, node_color='lightblue', node_size=2000, font_size=16, font_color='black', font_weight='bold')

    # Save the graph as an SVG file
    plt.savefig("graph.svg", format="svg")

# Report which structs and struct fields are private to the module
def privacy_report(module, graph, parsed_files: List[File], ambiguous_fields: Set[str]):

    all_structs: List[Struct] = []
    all_funcs: List[Function] = []

    for file in parsed_files:
        if file.module == module:
            all_structs += file.structs
            all_funcs += file.functions

    (num_private_funcs, num_total_funcs) = privacy_report_functions(graph, module, all_funcs)
    (num_private_fields, num_total_fields) = privacy_report_structs(graph, module, all_structs, ambiguous_fields)

    if num_private_fields != 0:
        private_pct = round((num_private_fields / num_total_fields) * 100, 2)
        print(f"{num_private_fields} of {num_total_fields} non-ambiguous struct fields ({private_pct}%) are private")

    if num_private_funcs != 0:
        private_pct = round((num_private_funcs / num_total_funcs) * 100, 2)
        print(f"{num_private_funcs} of {num_total_funcs} functions ({private_pct}%) are private")

def generate_dependency_file(graph: nx.DiGraph):
    with open("dep_file.new", 'w') as f:
        f.write("# ====================================================== #\n")
        f.write("# WiredTiger has the following inter-module dependencies #\n")
        f.write("# ====================================================== #\n")
        for module in sorted(graph.nodes()):
            incoming_edges = graph.out_edges(module)
            for (caller, callee) in sorted(incoming_edges):
                if caller == AMBIG_NODE or callee == AMBIG_NODE:
                    # Don't report the ambiguous node
                    continue
                f.write(f"{caller} -> {callee}\n")
