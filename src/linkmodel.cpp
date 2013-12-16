#include "linkmodel.h"

#include <QtCore/QDateTime>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkReply>

#include "utils.h"
#include "parser.h"

LinkModel::LinkModel(QObject *parent) :
    AbstractListModelManager(parent), m_section(HotSection), m_searchSort(RelevanceSort),
    m_searchTimeRange(AllTime), m_reply(0)
{
    QHash<int, QByteArray> roles;
    roles[FullnameRole] = "fullname";
    roles[AuthorRole] = "author";
    roles[CreatedRole] = "created";
    roles[SubredditRole] = "subreddit";
    roles[ScoreRole] = "score";
    roles[LikesRole] = "likes";
    roles[CommentsCountRole] = "commentsCount";
    roles[TitleRole] = "title";
    roles[DomainRole] = "domain";
    roles[ThumbnailUrlRole] = "thumbnailUrl";
    roles[TextRole] = "text";
    roles[PermalinkRole] = "permalink";
    roles[UrlRole] = "url";
    roles[IsStickyRole] = "isSticky";
    roles[IsNSFWRole] = "isNSFW";
    setRoleNames(roles);
}

int LinkModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_linkList.count();
}

QVariant LinkModel::data(const QModelIndex &index, int role) const
{
    Q_ASSERT_X(index.row() < m_linkList.count(), Q_FUNC_INFO, "index out of range");

    const LinkObject link = m_linkList.at(index.row());

    switch (role) {
    case FullnameRole: return link.fullname();
    case AuthorRole:
        switch (link.distinguished()) {
        case LinkObject::DistinguishedByModerator: return link.author() + " [M]";
        case LinkObject::DistinguishedByAdmin: return link.author() + " [A]";
        case LinkObject::DistinguishedBySpecial: return link.author() + " [?]";
        default: return link.author();
        }
    case CreatedRole: return Utils::getTimeDiff(link.created());
    case SubredditRole: return link.subreddit();
    case ScoreRole: return link.score();
    case LikesRole: return link.likes();
    case CommentsCountRole: return link.commentsCount();
    case TitleRole: return link.title();
    case DomainRole: return link.domain();
    case ThumbnailUrlRole: return link.thumbnailUrl();
    case TextRole: return link.text();
    case PermalinkRole: return link.permalink();
    case UrlRole: return link.url();
    case IsStickyRole: return link.isSticky();
    case IsNSFWRole: return link.isNSFW();
    default:
        qCritical("LinkModel::data(): Invalid role");
        return QVariant();
    }
}

QString LinkModel::title() const
{
    return m_title;
}

LinkModel::Section LinkModel::section() const
{
    return m_section;
}

void LinkModel::setSection(LinkModel::Section section)
{
    if (m_section != section) {
        m_section = section;
        emit sectionChanged();
    }
}

QString LinkModel::subreddit() const
{
    return m_subreddit;
}

void LinkModel::setSubreddit(const QString &subreddit)
{
    if (m_subreddit != subreddit) {
        m_subreddit = subreddit;
        emit subredditChanged();
    }
}

QString LinkModel::searchQuery() const
{
    return m_searchQuery;
}

void LinkModel::setSearchQuery(const QString &query)
{
    if (m_searchQuery != query) {
        m_searchQuery = query;
        emit searchQueryChanged();
    }
}

LinkModel::SearchSortType LinkModel::searchSort() const
{
    return m_searchSort;
}

void LinkModel::setSearchSort(LinkModel::SearchSortType sort)
{
    if (m_searchSort != sort) {
        m_searchSort = sort;
        emit searchSortChanged();
    }
}

LinkModel::SearchTimeRange LinkModel::searchTimeRange() const
{
    return m_searchTimeRange;
}

void LinkModel::setSearchTimeRange(LinkModel::SearchTimeRange timeRange)
{
    if (m_searchTimeRange != timeRange) {
        m_searchTimeRange = timeRange;
        emit  searchTimeRangeChanged();
    }
}

void LinkModel::refresh(bool refreshOlder)
{
    if (m_reply != 0) {
        m_reply->disconnect();
        m_reply->deleteLater();
        m_reply = 0;
    }

    QString relativeUrl = "/";
    QHash<QString,QString> parameters;
    parameters["limit"] = "50";

    if (m_section == SearchSection) {
        parameters["q"] = m_searchQuery;
        parameters["sort"] = getSearchSortString(m_searchSort);
        parameters["t"] = getSearchTimeRangeString(m_searchTimeRange);
        relativeUrl += "search";
    } else {
        if (!m_subreddit.isEmpty())
            relativeUrl += "r/" + m_subreddit + "/";
        relativeUrl += getSectionString(m_section);
    }

    if (!m_linkList.isEmpty()) {
        if (refreshOlder) {
            parameters["count"] = QString::number(m_linkList.count());
            parameters["after"] = m_linkList.last().fullname();
        } else {
            beginRemoveRows(QModelIndex(), 0, m_linkList.count() - 1);
            m_linkList.clear();
            endRemoveRows();
        }
    }

    connect(manager(), SIGNAL(networkReplyReceived(QNetworkReply*)),
            SLOT(onNetworkReplyReceived(QNetworkReply*)));
    manager()->createRedditRequest(QuickdditManager::GET, relativeUrl, parameters);

    m_title = relativeUrl;
    emit titleChanged();
    setBusy(true);
}

void LinkModel::changeVote(const QString &fullname, VoteManager::VoteType voteType)
{
    for (int i = 0; i < m_linkList.count(); ++i) {
        LinkObject link = m_linkList.at(i);

        if (link.fullname() == fullname) {
            int oldLikes = link.likes();
            switch (voteType) {
            case VoteManager::Upvote:
                link.setLikes(1); break;
            case VoteManager::Downvote:
                link.setLikes(-1); break;
            case VoteManager::Unvote:
                link.setLikes(0); break;
            }
            link.setScore(link.score() + (link.likes() - oldLikes));
            emit dataChanged(index(i), index(i));
            break;
        }
    }
}

void LinkModel::onNetworkReplyReceived(QNetworkReply *reply)
{
    disconnect(manager(), SIGNAL(networkReplyReceived(QNetworkReply*)),
               this, SLOT(onNetworkReplyReceived(QNetworkReply*)));
    if (reply != 0) {
        m_reply = reply;
        m_reply->setParent(this);
        connect(m_reply, SIGNAL(finished()), SLOT(onFinished()));
    } else {
        setBusy(false);
    }
}

void LinkModel::onFinished()
{
    if (m_reply->error() == QNetworkReply::NoError) {
        const QList<LinkObject> links = Parser::parseLinkList(m_reply->readAll());
        if (!links.isEmpty()) {
            beginInsertRows(QModelIndex(), m_linkList.count(), m_linkList.count() + links.count() - 1);
            m_linkList.append(links);
            endInsertRows();
        }
    } else {
        emit error(m_reply->errorString());
    }

    m_reply->deleteLater();
    m_reply = 0;
    setBusy(false);
}

QString LinkModel::getSectionString(Section section)
{
    switch (section) {
    case HotSection: return "hot";
    case NewSection: return "new";
    case RisingSection: return "rising";
    case ControversialSection: return "controversial";
    case TopSection: return "top";
    default:
        qWarning("LinkModel::getSectionString(): Invalid section");
        return "";
    }
}

QString LinkModel::getSearchSortString(SearchSortType sort)
{
    switch (sort) {
    case RelevanceSort: return "relevance";
    case NewSort: return "new";
    case HotSort: return "hot";
    case TopSort: return "top";
    case CommentsSort: return "comments";
    default:
        qWarning("LinkModel::getSearchSortString(): Invalid sort");
        return "";
    }
}

QString LinkModel::getSearchTimeRangeString(SearchTimeRange timeRange)
{
    switch (timeRange) {
    case AllTime: return "all";
    case Hour: return "hour";
    case Day: return "day";
    case Week: return "week";
    case Month: return "month";
    case Year: return "year";
    default:
        qWarning("LinkModel::getSearchTimeRangeString(): Invalid time range");
        return "";
    }
}
