#include "fml.h"

#include <boost/algorithm/string.hpp>
#include <boost/tokenizer.hpp>

namespace FML {


///////////////////////////////////////////////////////////////////////////////
// Utilities

bool fml_convert(const std::string& input, bool& b)
{
    std::string names[5][2] = { { "1", "0" },
    { "y", "n" },
    { "yes", "no" },
    { "true", "false" },
    { "on", "off" } };
    std::string processedInput = input;
    boost::trim(processedInput);
    boost::to_lower(processedInput);
    for(int i=0;i<5;i++) {
        if(names[i][0] == processedInput) {
            b = true;
            return true;
        }

        if(names[i][1] == processedInput) {
            b = false;
            return true;
        }
    }
    return false;
}

bool fml_convert(const std::string& input, std::string& output) {
    output = input;
    return true;
}

std::string fml_int2str(int v)
{
    std::stringstream ss;
    ss << v;
    return ss.str();
}

///////////////////////////////////////////////////////////////////////////////
// Node

Node::~Node()
{
    for(NodeList::iterator it = m_children.begin(); it != m_children.end(); ++it)
        delete (*it);
}

std::string Node::what() const
{
    if(m_parser)
        return m_parser->what();
    return std::string();
}

Node* Node::at(const std::string& childTag) const
{
    for(NodeList::const_iterator it = m_children.begin(); it != m_children.end(); ++it) {
        if((*it)->tag() == childTag)
            return (*it);
    }
    return NULL;
}

Node* Node::at(int pos) const
{
    if(pos < 0 || pos >= size())
        return NULL;
    return m_children[pos];
}

void Node::addNode(Node *node)
{
    if(node->hasTag() && node->hasValue()) {
        // remove nodes wit the same tag
        for(NodeList::iterator it = m_children.begin(); it != m_children.end(); ++it) {
            if((*it)->tag() == node->tag()) {
                delete (*it);
                m_children.erase(it);
                break;
            }
        }
    }
    m_children.push_back(node);
    node->setParent(this);
}

std::string Node::generateErrorMessage(const std::string& message) const
{
    std::stringstream ss;
    ss << "FML error";
    if(!(what().empty()))
        ss << " in '" << what() << "'";
    if(m_line > 0) {
        ss << " at line " << m_line;
        if(hasTag())
            ss << ", in node '" << tag() << "'";
    }
    ss << ": " << message;
    return ss.str();
}

std::string Node::emitValue()
{
    std::string tmpValue = value();

    bool shouldQuote = false;
    if(tmpValue.find_first_of("\\") != std::string::npos) {
        boost::replace_all(tmpValue, "\\", "\\\\");
        shouldQuote = true;
    }
    if(tmpValue.find_first_of("\"") != std::string::npos) {
        boost::replace_all(tmpValue, "\"", "\\\"");
        shouldQuote = true;
    }
    if(tmpValue.find_first_of("\n") != std::string::npos) {
        boost::replace_all(tmpValue, "\n", "\\n");
        shouldQuote = true;
    }

    if(shouldQuote) {
        tmpValue.append("\"");
        tmpValue.insert(0, "\"");
    }

    return tmpValue;
}

std::string Node::emit(int depth)
{
    std::stringstream ss;
    std::stringstream inlinestr;
    bool shouldInline = false;

    for(int i=1;i<depth;++i)
        ss << "  ";

    if(depth > 0) {
        shouldInline = true;

        if(hasTag())
            ss << tag();

        if(hasValue()) {
            if(hasTag())
                ss << ": ";
            else
                ss << "- ";
            ss << emitValue();
            ss << std::endl;
            shouldInline = false;
        }

        if(size() > 8 || size() == 0)
            shouldInline = false;
    }

    if(shouldInline) {
        inlinestr << "[";
        for(NodeList::const_iterator it = m_children.begin(); it != m_children.end(); ++it) {
            Node* child = (*it);
            if(child->hasTag() || ss.str().length() > 31) {
                shouldInline = false;
                break;
            } else {
                if(it != m_children.begin())
                    inlinestr << ", ";
                inlinestr << child->emitValue();
            }
        }
        inlinestr << "]";
    }

    if(shouldInline) {
        ss << ": " << inlinestr.str() << std::endl;
    } else {
        if(!hasValue())
            ss << std::endl;

        for(NodeList::const_iterator it = m_children.begin(); it != m_children.end(); ++it)
            ss << (*it)->emit(depth+1);
    }

    return ss.str();
}

///////////////////////////////////////////////////////////////////////////////
// Parser

Parser::~Parser()
{
    if(m_rootNode)
        delete m_rootNode;
}

void Parser::load(std::istream& in)
{
    // initialize root node
    if(m_rootNode)
        delete m_rootNode;
    m_rootNode = new Node();
    m_rootNode->setTag("root");

    m_currentParent = m_rootNode;
    m_currentDepth = 0;
    m_currentLine = 0;
    m_multilineMode = DONT_MULTILINE;
    m_multilineData.clear();

    while(in.good() && !in.eof()) {
        m_currentLine++;
        std::string line;
        std::getline(in, line);
        parseLine(line);
    }

    // stop multilining if enabled
    if(isMultilining())
        stopMultilining();
}

void Parser::parseLine(std::string& line)
{
    // process multiline data first
    if(isMultilining() && parseMultiline(line))
        return;

    // calculate depth
    std::size_t numSpaces = line.find_first_not_of(' ');

    // trim left whitespaces
    boost::trim_left(line);

    // skip comment lines
    if(line[0] == '#')
        return;

    // skip empty lines
    if(line.empty())
        return;

    int depth = 0;
    if(numSpaces != std::string::npos) {
        depth = numSpaces / 2;
        // check for syntax error
        if(numSpaces % 2 != 0) {
            throwError("file must be idented every 2 whitespaces", m_currentLine);
            return;
        }
    }

    // a depth above
    if(depth == m_currentDepth+1) {
        // change current parent to the previous added node
        m_currentParent = m_previousNode;
    // a depth below, change parent to previus parent and add new node inside previuos parent
    } else if(depth < m_currentDepth) {
        // change current parent to the the new depth parent
        for(int i=0;i<m_currentDepth-depth;++i)
            m_currentParent = m_currentParent->parent();
    // else if nots the same depth it's a syntax error
    } else if(depth != m_currentDepth) {
        throwError("invalid indentation level", m_currentLine);
        return;
    }

    // update current depth
    m_currentDepth = depth;

    // add node
    Node *node = parseNode(line);
    m_currentParent->addNode(node);
    m_previousNode = node;
}

Node *Parser::parseNode(std::string& line)
{
    // determine node tag and value
    std::string tag;
    std::string value;

    // its a node that has tag and possible a value
    std::size_t dotsPos = line.find_first_of(':');
    if(dotsPos != std::string::npos) {
        tag = line.substr(0, dotsPos);
        value = line.substr(dotsPos+1);
    }
    // its a node that has a value but no tag
    else if(line[0] == '-') {
        value = line.substr(1);
    }
    // its a node that has only a tag
    else {
        tag = line;
    }

    // trim the tag and value
    boost::trim(tag);
    boost::trim(value);

    // create the node
    Node *node = new Node(this);
    node->setLine(m_currentLine);
    node->setTag(tag);

    // process node value
    if(!value.empty()) {
        // multiline text scalar
        if(value[0] == '|') {
            startMultilining(value);
        }
        // sequence
        else if(value[0] == '[') {
            if(boost::ends_with(value, "]")) {
                value.erase(value.length()-1, 1);
                value.erase(0, 1);
                boost::trim(value);
                boost::tokenizer<boost::escaped_list_separator<char> > tokens(value);
                for(boost::tokenizer<boost::escaped_list_separator<char> >::iterator it = tokens.begin(); it != tokens.end(); ++it) {
                    std::string tmp = (*it);
                    boost::trim(tmp);
                    if(!tmp.empty()) {
                        Node *child = new Node(this);
                        child->setLine(m_currentLine);
                        child->setValue(tmp);
                        node->addNode(child);
                    }
                }
            } else
                throwError("missing ']' in sequence", m_currentLine);
        }
        // text scalar
        else {
            node->setValue(parseTextScalar(value));
        }
    }

    return node;
}

std::string Parser::parseTextScalar(std::string value)
{
    if(value[0] == '"' && value[value.length()-1] == '"') {
        value =  value.substr(1, value.length()-2);
        // escape characters
        boost::replace_all(value, "\\\\", "\\");
        boost::replace_all(value, "\\\"", "\"");
        boost::replace_all(value, "\\n", "\n");
    }
    return value;
}

void Parser::startMultilining(const std::string& param)
{
    m_multilineMode = MULTILINE_DONT_FOLD;
    m_currentDepth++;
    if(param.length() == 2) {
        switch(param[1]) {
            case '-':
                m_multilineMode = MULTILINE_FOLD_BLOCK;
                break;
            case '+':
                m_multilineMode = MULTILINE_FOLD_FLOW;
                break;
            default:
                throwError("invalid multiline identifier", m_currentLine);
                break;
        }
    }
}

void Parser::stopMultilining()
{
    // remove all new lines at the end
    if(m_multilineMode == MULTILINE_DONT_FOLD || m_multilineMode == MULTILINE_FOLD_BLOCK) {
        while(true) {
            int lastPos = m_multilineData.length()-1;
            if(m_multilineData[lastPos] != '\n')
                break;
            m_multilineData.erase(lastPos, 1);
        }
    }

    if(m_multilineMode == MULTILINE_FOLD_BLOCK)
        m_multilineData.append("\n");

    m_previousNode->setValue(m_multilineData);
    m_multilineMode = DONT_MULTILINE;
    m_currentDepth--;
    m_multilineData.clear();
}

bool Parser::parseMultiline(std::string line)
{
    // calculate depth
    std::size_t numSpaces = line.find_first_not_of(' ');

    // depth above or equal current depth, add the text to the multiline
    if(numSpaces != std::string::npos && (int)numSpaces >= m_currentDepth*2) {
        m_multilineData += parseTextScalar(line.substr(m_currentDepth*2)) + "\n";
        return true;
        // depth below the current depth, check if it is a node
    } else if(numSpaces == std::string::npos || (int)numSpaces < m_currentDepth*2) {
        // if it has contents, its a node, then we must end multiline
        if(line.find_first_not_of(' ') != std::string::npos) {
            stopMultilining();
            // the line still have a node on it
        }
        // no contents, just an extra line
        else {
            m_multilineData += "\n";
            return true;
        }
    }
    return false;
}

void Parser::throwError(const std::string& message, int line)
{
    std::stringstream ss;
    ss << "FML syntax error";
    if(!m_what.empty())
        ss  << " in '" << m_what << "'";
    if(line > 0)
        ss << " at line " << line;
    ss << ": "  << message;
    throw Exception(ss.str());
}

} // namespace FML {