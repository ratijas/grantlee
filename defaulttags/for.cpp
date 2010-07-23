/*
  This file is part of the Grantlee template system.

  Copyright (c) 2009,2010 Stephen Kelly <steveire@gmail.com>

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2 of the Licence, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "for.h"

#include <QtCore/QStringList>

#include "parser.h"
#include "exception.h"


ForNodeFactory::ForNodeFactory()
{

}

Node* ForNodeFactory::getNode( const QString &tagContent, Parser *p ) const
{
  QStringList expr = smartSplit( tagContent );

  if ( expr.size() < 4 ) {
    throw Grantlee::Exception( TagSyntaxError,
        QString::fromLatin1( "'for' statements should have at least four words: %1" ).arg( tagContent ) );
  }

  expr.takeAt( 0 );
  QStringList vars;

  int reversed = ForNode::IsNotReversed;
  if ( expr.last() == QLatin1String( "reversed" ) ) {
    reversed = ForNode::IsReversed;
    expr.removeLast();
  }

  if ( expr.mid( expr.size() - 2 ).at( 0 ) != QLatin1String( "in" ) ) {
    throw Grantlee::Exception( TagSyntaxError,
      QString::fromLatin1( "'for' statements should use the form 'for x in y': %1" ).arg( tagContent ) );
  }

  Q_FOREACH( const QString &arg, expr.mid( 0, expr.size() - 2 ) ) {
    vars << arg.split( QLatin1Char( ',' ), QString::SkipEmptyParts );
  }

  Q_FOREACH( const QString &var, vars ) {
    if ( var.isNull() )
      throw Grantlee::Exception( TagSyntaxError, QLatin1String( "'for' tag received invalid argument" ) );
  }

  FilterExpression fe( expr.last(), p );

  ForNode *n = new ForNode( vars, fe, reversed, p );

  NodeList loopNodes = p->parse( n, QStringList() << QLatin1String( "empty" ) << QLatin1String( "endfor" ) );
  n->setLoopList( loopNodes );

  NodeList emptyNodes;
  if ( p->takeNextToken().content.trimmed() == QLatin1String( "empty" ) ) {
    emptyNodes = p->parse( n, QLatin1String( "endfor" ) );
    n->setEmptyList( emptyNodes );
    // skip past the endfor tag
    p->removeNextToken();
  }

  return n;
}



ForNode::ForNode( QStringList loopVars,
                  FilterExpression fe,
                  int reversed,
                  QObject *parent )
    : Node( parent ),
    m_loopVars( loopVars ),
    m_filterExpression( fe ),
    m_isReversed( reversed )
{

}

void ForNode::setLoopList( NodeList loopNodeList )
{
  m_loopNodeList = loopNodeList;
}

void ForNode::setEmptyList( NodeList emptyList )
{
  m_emptyNodeList = emptyList;
}

// some magic variables injected into the context while rendering.
static const QString forloop = QLatin1String( "forloop" );
static const QString parentloop = QLatin1String( "parentloop" );
static const QString counter0 = QLatin1String( "counter0" );
static const QString counter = QLatin1String( "counter" );
static const QString revcounter0 = QLatin1String( "revcounter0" );
static const QString revcounter = QLatin1String( "revcounter" );
static const QString first = QLatin1String( "first" );
static const QString last = QLatin1String( "last" );

void ForNode::insertLoopVariables( Context *c, int listSize, int i )
{
  QVariantHash forloopHash = c->lookup( QLatin1String( "forloop" ) ).toHash();
  forloopHash.insert( counter0, i );
  forloopHash.insert( counter, i + 1 );
  forloopHash.insert( revcounter, listSize - i );
  forloopHash.insert( revcounter0, listSize - i - 1 );
  forloopHash.insert( first, ( i == 0 ) );
  forloopHash.insert( last, ( i == listSize - 1 ) );
  c->insert( forloop, forloopHash );
}

void ForNode::renderLoop( OutputStream *stream, Context *c )
{
  for ( int j = 0; j < m_loopNodeList.size();j++ ) {
    m_loopNodeList[j]->render( stream, c );
  }
}

void ForNode::handleHashItem( OutputStream *stream, Context *c, QString key, QVariant value, int listSize, int i, bool unpack )
{
  QVariantList list;
  insertLoopVariables( c, listSize, i );

  if ( !unpack ) {
    // Iterating over a hash but not unpacking it.
    // convert each key-value pair to a list and insert it in the context.
    list << key << value;
    c->insert( m_loopVars.at( 0 ), list );
    list.clear();
  } else {
    c->insert( m_loopVars.at( 0 ), key );
    c->insert( m_loopVars.at( 1 ), value );
  }
  renderLoop( stream, c );
}

void ForNode::iterateHash( OutputStream *stream, Context *c, QVariantHash varHash, bool unpack )
{
  int listSize = varHash.size();
  int i = 0;
  QVariantList list;

  QHashIterator<QString, QVariant> it( varHash );
  if ( m_isReversed == IsReversed ) {
    while ( it.hasPrevious() ) {
      it.previous();
      handleHashItem( stream, c, it.key(), it.value(), listSize, i, unpack );
      ++i;
    }
  } else {
    while ( it.hasNext() ) {
      it.next();
      handleHashItem( stream, c, it.key(), it.value(), listSize, i, unpack );
      ++i;
    }
  }
}

void ForNode::render( OutputStream *stream, Context *c )
{
  QVariantHash forloopHash;

  QVariant parentLoopVariant = c->lookup( forloop );
  if ( parentLoopVariant.isValid() ) {
    // This is a nested loop.
    forloopHash = parentLoopVariant.toHash();
    forloopHash.insert( parentloop, parentLoopVariant.toHash() );
    c->insert( forloop, forloopHash );
  }

  bool unpack = m_loopVars.size() > 1;

  c->push();

//   if ( var.type() == QVariant::Hash ) {
//     QVariantHash varHash = var.toHash();
//     result = iterateHash( c, varHash, unpack );
//     c->pop();
//     return result;
//   }

  // If it's an iterable type, iterate, otherwise it's a list of one.
  QVariantList varList = m_filterExpression.toList( c );
  NodeList nodeList;
  int listSize = varList.size();

  if ( listSize < 1 ) {
    c->pop();
    return m_emptyNodeList.render( stream, c );
  }

  for ( int i = 0; i < listSize; i++ ) {
    insertLoopVariables( c, listSize, i );

    int index = i;
    if ( m_isReversed == IsReversed ) {
      index = listSize - i - 1;
    }

    if ( unpack ) {
      if ( varList[index].type() == QVariant::List ) {
        QVariantList vList = varList[index].toList();
        int varsSize = qMin( m_loopVars.size(), vList.size() );
        int j = 0;
        for ( ; j < varsSize; ++j ) {
          c->insert( m_loopVars.at( j ), vList.at( j ) );
        }
        // If any of the named vars don't have an item in the context,
        // insert an invalid object for them.
        for ( ; j < m_loopVars.size(); ++j ) {
          c->insert( m_loopVars.at( j ), QVariant() );
        }

      } else {
        // We don't have a hash, but we have to unpack several values from each item
        // in the list. And each item in the list is not itself a list.
        // Probably have a list of objects that we're taking properties from.
        Q_FOREACH( const QString &loopVar, m_loopVars ) {
          c->push();
          c->insert( QLatin1String( "var" ), varList[index] );
          QVariant v = FilterExpression( QLatin1String( "var." ) + loopVar, 0 ).resolve( c );
          c->pop();
          c->insert( loopVar, v );
        }
      }
    } else {
      c->insert( m_loopVars[0], varList[index] );
    }
    renderLoop( stream, c );
  }
  c->pop();
}

