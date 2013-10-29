#include "PropertyMap.h"
#include <errors.h>
#include <fstream>
#include "ioutils.h"
#if HAVE_TINYXML
#include <tinyxml.h>
#else
class TiXmlElement {};
#endif

using namespace std;

bool ReadString(std::istream& in,string& str,const std::string& delims)
{
  EatWhitespace(in);
  if(!in) {
    printf("ReadValue: hit end of file\n");
    return false;
  }
  if(in.peek() == '"') {
    //beginning of string
    if(!InputQuotedString(in,str)) {
      printf("ReadValue: unable to read quoted string\n");
      return false;
    }
    return true;
  }
  else if(in.peek() == '\'') {
    //character: TODO: translate escapes properly
    in.get();
    char c=in.get();
    str = c;
    char end=in.get();
    if(end != '\'') {
      printf("ReadValue: character not delimited properly\n");
      return false;
    }
    return true;
  }
  else {
    if(delims.empty())
      in >> str;
    else {
      while(in && delims.find(in.peek()) == std::string::npos && !isspace(in.peek())) {
	str += in.get();
      }
    }
    if(str.empty()) {
      return false;
    }
    return true;
  }
}

PropertyMap::PropertyMap(const std::map<std::string,std::string>& rhs)
  :std::map<std::string,std::string>(rhs)
{}


bool PropertyMap::remove(const std::string& key)
{
  iterator i=find(key);
  if(i == end()) return false;
  erase(i);
  return true;
}

bool PropertyMap::LoadJSON(istream& in)
{
  clear();
  EatWhitespace(in);
  if(in.peek()!='{') {
    fprintf(stderr,"PropertyMap: unable to read arrays or single values\n");
    return false;
  }
  in.get();
  //read list
  while(true) {
    string key;
    string value;
    if(!InputQuotedString(in,key)) {
      fprintf(stderr,"PropertyMap: read failed on item %d\n",size());
      return false;
    }
    EatWhitespace(in);
    if(in.peek() != ':') {
      std::cerr<<"Map missing a colon-separator between key-value pair "<<key<<std::endl;
      return false;
    }
    in.get();
    if(!ReadString(in,value,",}")) {
      std::cerr<<"PropertyMap: couldn't read value for key "<<key<<std::endl;
      return false;
    }
    (*this)[key] = value;
    EatWhitespace(in);
    int c = in.get();
    if(c == '}') return true;
    if(c != ',') {
      std::cerr<<"Map entries not separated by commas, character "<<char(c)<<" item "<<size()<<std::endl;
      return false;
    }
  }
  return false;
}

bool PropertyMap::SaveJSON(ostream& out) const
{
  out<<"{"<<endl;
  for(map<string,string>::const_iterator i=begin();i!=end();i++) {
    out<<"   ";
    OutputQuotedString(out,i->first);
    out<<" : ";
    OutputQuotedString(out,i->second);
    if(i != --end())
      out<<",";
    out<<endl;
  }
  out<<"}";
  return true;
}

void PropertyMap::Print(ostream& out) const
{
  out<<"{"<<endl;
  for(map<string,string>::const_iterator i=begin();i!=end();i++) {
    out<<"   "<<i->first<<" : ";
    OutputQuotedString(out,i->second);
    if(i != --end())
      out<<",";
    out<<endl;
  }
  out<<"}";
}

bool PropertyMap::Load(TiXmlElement* node)
{
#if  HAVE_TINYXML
  this->clear();
  TiXmlAttribute* attr = node->FirstAttribute();
  while(attr != NULL) {
    (*this)[attr->Name()] = attr->Value();
    attr = attr->Next();
  }
  return true;
#else 
  fprintf(stderr,"PropertyMap: Didn't compile KrisLibrary with TinyXml");
  return false;
#endif //HAVE_TINYXML
}

bool PropertyMap::Save(TiXmlElement* node) const
{
#if HAVE_TINYXML
  for(map<string,string>::const_iterator i=begin();i!=end();i++)
    node->SetAttribute(i->first.c_str(),i->second.c_str());
  return true;
#else 
  fprintf(stderr,"PropertyMap: Didn't compile KrisLibrary with TinyXml");
  return false;
#endif //HAVE_TINYXML
}




std::string PropertyMap::as(const std::string& key) const
{
  const_iterator i=find(key);
  if(i==end()) return "";
  return i->second;
}

bool PropertyMap::get(const std::string& key,std::string& value) const
{
  const_iterator i=find(key);
  if(i==end()) return false;
  value = i->second;
  return true;
}

void PropertyMap::set(const std::string& key,const std::string& value)
{
  (*this)[key] = value;
}


bool PropertyMap::getArray(const std::string& key,std::vector<std::string>& values) const
{
  const_iterator i=find(key);
  if(i==end()) return false;
  std::stringstream ss(i->second);
  string temp;
  values.resize(0);
  while(ss) {
    SafeInputString(ss,temp);
    values.push_back(temp);
  }
  return true;
}

std::vector<string> PropertyMap::asArray(const std::string& key) const
{
  std::vector<string> res;
  getArray(key,res);
  return res;
}

void PropertyMap::setArray(const std::string& key,const std::vector<std::string>& values)
{
  std::stringstream ss;
  for(size_t i=0;i<values.size();i++) {
    if(i > 0) ss<<" ";
    SafeOutputString(ss,values[i]);
  }
  set(key,ss.str());
}

bool PropertyMap::Load(const char* fn)
{
  ifstream in(fn,ios::in);
  if(!in) return false;
  if(!LoadJSON(in)) return false;
  in.close();
  return true;
}

bool PropertyMap::Save(const char* fn) const
{
  ofstream out(fn,ios::out);
  if(!out) return false;
  if(!SaveJSON(out)) return false;
  out.close();
  return true;
}
