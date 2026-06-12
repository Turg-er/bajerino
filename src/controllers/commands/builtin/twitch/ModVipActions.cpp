#include "controllers/commands/builtin/twitch/ModVipActions.hpp"

#include "common/Channel.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "providers/moltorino/MoltorinoAuth.hpp"
#include "providers/twitch/api/TwitchGql.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "util/PostToThread.hpp"
#include "util/Twitch.hpp"

#include <functional>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace chatterino::commands {
namespace {

struct ModVipActionInfo {
    QString authAction;
    QString failurePrefix;
};

ModVipActionInfo actionInfo(ModVipAction action)
{
    switch (action)
    {
        case ModVipAction::AddModerator:
            return {
                .authAction = "adding moderators",
                .failurePrefix = "Failed to add channel moderator - ",
            };

        case ModVipAction::RemoveModerator:
            return {
                .authAction = "removing moderators",
                .failurePrefix = "Failed to remove channel moderator - ",
            };

        case ModVipAction::AddVIP:
            return {
                .authAction = "adding VIP",
                .failurePrefix = "Failed to add VIP - ",
            };

        case ModVipAction::RemoveVIP:
            return {
                .authAction = "removing VIP",
                .failurePrefix = "Failed to remove VIP - ",
            };

        case ModVipAction::AddLeadModerator:
            return {
                .authAction = "adding lead moderator",
                .failurePrefix = "Failed to add lead moderator - ",
            };

        case ModVipAction::RemoveLeadModerator:
            return {
                .authAction = "removing lead moderator",
                .failurePrefix = "Failed to remove lead moderator - ",
            };

        case ModVipAction::AddEditor:
            return {
                .authAction = "adding editor",
                .failurePrefix = "Failed to add editor - ",
            };

        case ModVipAction::RemoveEditor:
            return {
                .authAction = "removing editor",
                .failurePrefix = "Failed to remove editor - ",
            };
    }

    return {};
}

QString permissionMessage()
{
    return u"This action needs broadcaster or lead mod permission."_s;
}

QString broadcasterPermissionMessage()
{
    return u"This action needs the broadcaster account."_s;
}

QString normalizeGqlRoleError(ModVipAction action, const QString &target,
                              const QString &error)
{
    const auto info = actionInfo(action);
    auto normalized = MoltorinoAuth::normalizeAuthError(info.authAction, error);
    const auto upper = normalized.toUpper();
    const auto lower = normalized.toLower();

    if (lower.contains("permission") || lower.contains("not authorized") ||
        lower.contains("not_authorized") || lower.contains("unauthorized") ||
        lower.contains("forbidden") || lower.contains("service error"))
    {
        if (action == ModVipAction::AddLeadModerator ||
            action == ModVipAction::RemoveLeadModerator ||
            action == ModVipAction::AddEditor ||
            action == ModVipAction::RemoveEditor)
        {
            return broadcasterPermissionMessage();
        }

        return permissionMessage();
    }

    switch (action)
    {
        case ModVipAction::AddModerator:
            if (upper.contains("ALREADY") && upper.contains("MOD"))
            {
                return QString("%1 is already a moderator of this channel.")
                    .arg(target);
            }
            if (upper.contains("VIP"))
            {
                return QString("%1 is currently a VIP, \"/unvip\" them and "
                               "retry this command.")
                    .arg(target);
            }
            break;

        case ModVipAction::RemoveModerator:
            if ((upper.contains("NOT") || upper.contains("MISSING")) &&
                upper.contains("MOD"))
            {
                return QString("%1 is not a moderator of this channel.")
                    .arg(target);
            }
            break;

        case ModVipAction::AddVIP:
            if (upper.contains("ALREADY") && upper.contains("VIP"))
            {
                return QString("%1 is already a VIP of this channel.")
                    .arg(target);
            }
            if (upper.contains("MODERATOR") || upper.contains("TARGET_IS_MOD"))
            {
                return QString("%1 is currently a moderator. Remove moderator "
                               "status before adding VIP.")
                    .arg(target);
            }
            break;

        case ModVipAction::RemoveVIP:
            if ((upper.contains("NOT") || upper.contains("MISSING")) &&
                upper.contains("VIP"))
            {
                return QString("%1 is not a VIP of this channel.").arg(target);
            }
            break;

        case ModVipAction::AddLeadModerator:
            if (upper.contains("ALREADY") || upper.contains("ROLE_ASSIGNED"))
            {
                return QString(
                           "%1 is already a lead moderator of this channel.")
                    .arg(target);
            }
            break;

        case ModVipAction::RemoveLeadModerator:
            if (upper.contains("UNASSIGNED") ||
                upper.contains("NOT_ASSIGNED") ||
                ((upper.contains("NOT") || upper.contains("MISSING")) &&
                 (upper.contains("LEAD") || upper.contains("ROLE") ||
                  upper.contains("MOD"))))
            {
                return QString("%1 is not a lead moderator of this channel.")
                    .arg(target);
            }
            break;

        case ModVipAction::AddEditor:
            if ((upper.contains("ALREADY") || upper.contains("EXISTS")) &&
                upper.contains("EDITOR"))
            {
                return QString("%1 is already an editor of this channel.")
                    .arg(target);
            }
            break;

        case ModVipAction::RemoveEditor:
            if ((upper.contains("NOT") || upper.contains("MISSING") ||
                 upper.contains("DOES_NOT_EXIST")) &&
                upper.contains("EDITOR"))
            {
                return QString("%1 is not an editor of this channel.")
                    .arg(target);
            }
            break;
    }

    return normalized;
}

void runGqlRoleMutation(
    ModVipAction action, const QString &channelId, const QString &target,
    const QString &token, const std::function<void()> &successCallback,
    const std::function<void(const QString &)> &failureCallback)
{
    switch (action)
    {
        case ModVipAction::AddModerator:
            TwitchGql::modUser(channelId, target, token, successCallback,
                               failureCallback);
            return;

        case ModVipAction::RemoveModerator:
            TwitchGql::unmodUser(channelId, target, token, successCallback,
                                 failureCallback);
            return;

        case ModVipAction::AddVIP:
            TwitchGql::grantVIP(channelId, target, token, successCallback,
                                failureCallback);
            return;

        case ModVipAction::RemoveVIP:
            TwitchGql::revokeVIP(channelId, target, token, successCallback,
                                 failureCallback);
            return;

        case ModVipAction::AddLeadModerator:
            TwitchGql::assignLeadModerator(channelId, target, token,
                                           successCallback, failureCallback);
            return;

        case ModVipAction::RemoveLeadModerator:
            TwitchGql::unassignLeadModerator(channelId, target, token,
                                             successCallback, failureCallback);
            return;

        case ModVipAction::AddEditor:
            TwitchGql::addEditorUser(channelId, target, token, successCallback,
                                     failureCallback);
            return;

        case ModVipAction::RemoveEditor:
            TwitchGql::removeEditorUser(channelId, target, token,
                                        successCallback, failureCallback);
            return;
    }
}

QString successMessage(ModVipAction action, const QString &target)
{
    switch (action)
    {
        case ModVipAction::AddLeadModerator:
            return QString("Added %1 as lead moderator.").arg(target);
        case ModVipAction::RemoveLeadModerator:
            return QString("Removed lead moderator from %1.").arg(target);
        case ModVipAction::AddEditor:
            return QString("Added %1 as editor.").arg(target);
        case ModVipAction::RemoveEditor:
            return QString("Removed editor from %1.").arg(target);
        default:
            return {};
    }
}

QString usageForAction(ModVipAction action)
{
    switch (action)
    {
        case ModVipAction::AddLeadModerator:
            return u"Usage: \"/leadmod <username>\" - Grant lead "
                   "moderator status."_s;
        case ModVipAction::RemoveLeadModerator:
            return u"Usage: \"/unleadmod <username>\" - Revoke "
                   "lead moderator status."_s;
        case ModVipAction::AddEditor:
            return u"Usage: \"/editor <username>\" - Add a channel editor."_s;
        case ModVipAction::RemoveEditor:
            return u"Usage: \"/uneditor <username>\" - Remove a channel editor."_s;
        default:
            return {};
    }
}

bool actionNeedsTargetUserId(ModVipAction action)
{
    return action == ModVipAction::AddLeadModerator ||
           action == ModVipAction::RemoveLeadModerator;
}

void addRoleFailureMessage(const ChannelPtr &channel, ModVipAction action,
                           const QString &target, const QString &error)
{
    runInGuiThread([channel, action, target, error] {
        if (channel != nullptr)
        {
            const auto info = actionInfo(action);
            channel->addSystemMessage(
                info.failurePrefix +
                normalizeGqlRoleError(action, target, error));
        }
    });
}

void runElevatedRoleMutation(const ChannelPtr &channel, ModVipAction action,
                             const QString &channelId,
                             const QString &targetValue,
                             const QString &targetDisplay, const QString &token)
{
    runGqlRoleMutation(
        action, channelId, targetValue, token,
        [channel, action, targetDisplay] {
            runInGuiThread(
                [channel, message = successMessage(action, targetDisplay)] {
                    if (channel != nullptr && !message.isEmpty())
                    {
                        channel->addSystemMessage(message);
                    }
                });
        },
        [channel, action, targetDisplay](const QString &error) {
            addRoleFailureMessage(channel, action, targetDisplay, error);
        });
}

QString runElevatedRoleCommand(const CommandContext &ctx, ModVipAction action)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.twitchChannel == nullptr)
    {
        ctx.channel->addSystemMessage(
            "This role command only works in Twitch channels.");
        return "";
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }

    const auto channelId = ctx.twitchChannel->roomId();
    if (channelId.isEmpty())
    {
        ctx.channel->addSystemMessage(
            "Channel ID is still loading. Try again in a moment.");
        return "";
    }

    const auto channelName = ctx.twitchChannel->getName();
    QString authError;
    const auto auth = MoltorinoAuth::resolveSavedBroadcasterToken(
        channelId, channelName, &authError);
    if (!auth.hasToken())
    {
        const auto info = actionInfo(action);
        ctx.channel->addSystemMessage(
            authError.isEmpty()
                ? MoltorinoAuth::authRequiredMessage(info.authAction)
                : authError);
        return "";
    }

    auto target = ctx.words.at(1).trimmed();
    stripChannelName(target);
    if (target.isEmpty())
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }

    auto [targetLogin, targetUserId] = parseUserNameOrID(target);
    if (targetLogin.isEmpty() && targetUserId.isEmpty())
    {
        ctx.channel->addSystemMessage(usageForAction(action));
        return "";
    }
    if (!targetLogin.isEmpty())
    {
        targetLogin = targetLogin.trimmed().toLower();
        if (!twitchUserNameRegexp().match(targetLogin).hasMatch())
        {
            ctx.channel->addSystemMessage(
                QString("Invalid Twitch username: %1").arg(targetLogin));
            return "";
        }
    }

    if (actionNeedsTargetUserId(action))
    {
        if (!targetUserId.isEmpty())
        {
            if (targetUserId == channelId)
            {
                ctx.channel->addSystemMessage(
                    "The broadcaster already owns the channel.");
                return "";
            }

            runElevatedRoleMutation(ctx.channel, action, channelId,
                                    targetUserId, targetUserId, auth.token);
            return "";
        }

        TwitchGql::getUserByLogin(
            targetLogin, auth.token,
            [channel{ctx.channel}, action, channelId, targetLogin,
             token = auth.token](std::optional<GqlUser> user) mutable {
                if (!user)
                {
                    runInGuiThread([channel, targetLogin] {
                        if (channel != nullptr)
                        {
                            channel->addSystemMessage(
                                QString("Could not look up user: %1.")
                                    .arg(targetLogin));
                        }
                    });
                    return;
                }

                if (user->id == channelId)
                {
                    runInGuiThread([channel] {
                        if (channel != nullptr)
                        {
                            channel->addSystemMessage(
                                "The broadcaster already owns the channel.");
                        }
                    });
                    return;
                }

                const auto display = user->displayName.isEmpty()
                                         ? user->login
                                         : user->displayName;
                runElevatedRoleMutation(channel, action, channelId, user->id,
                                        display, token);
            },
            [channel{ctx.channel}, action, targetLogin](const QString &error) {
                addRoleFailureMessage(channel, action, targetLogin, error);
            });
        return "";
    }

    if (targetLogin.isEmpty())
    {
        ctx.channel->addSystemMessage("This command needs a Twitch username.");
        return "";
    }

    if (targetLogin.compare(channelName, Qt::CaseInsensitive) == 0)
    {
        ctx.channel->addSystemMessage(
            "The broadcaster already owns the channel.");
        return "";
    }

    runElevatedRoleMutation(ctx.channel, action, channelId, targetLogin,
                            targetLogin, auth.token);
    return "";
}

}  // namespace

bool tryRunModVipActionWithLeadModGql(const CommandContext &ctx,
                                      const QString &target,
                                      ModVipAction action)
{
    if (ctx.channel == nullptr || ctx.twitchChannel == nullptr)
    {
        return false;
    }

    if (ctx.twitchChannel->isBroadcaster() || !ctx.twitchChannel->isMod())
    {
        return false;
    }

    if (ctx.twitchChannel->roomId().isEmpty())
    {
        ctx.channel->addSystemMessage(
            "Channel ID is still loading. Try again in a moment.");
        return true;
    }

    const auto info = actionInfo(action);
    QString authError;
    const auto auth = MoltorinoAuth::resolveModerationToken(
        ctx.twitchChannel->roomId(), ctx.twitchChannel->getName(), &authError);
    if (!auth.hasToken())
    {
        ctx.channel->addSystemMessage(
            authError.isEmpty()
                ? MoltorinoAuth::authRequiredMessage(info.authAction)
                : authError);
        return true;
    }

    runGqlRoleMutation(
        action, ctx.twitchChannel->roomId(), target, auth.token, [] {},
        [channel{ctx.channel}, action, target,
         prefix = info.failurePrefix](const QString &error) {
            runInGuiThread([channel, action, target, prefix, error] {
                if (channel != nullptr)
                {
                    channel->addSystemMessage(
                        prefix + normalizeGqlRoleError(action, target, error));
                }
            });
        });

    return true;
}

QString addLeadModerator(const CommandContext &ctx)
{
    return runElevatedRoleCommand(ctx, ModVipAction::AddLeadModerator);
}

QString removeLeadModerator(const CommandContext &ctx)
{
    return runElevatedRoleCommand(ctx, ModVipAction::RemoveLeadModerator);
}

QString addEditor(const CommandContext &ctx)
{
    return runElevatedRoleCommand(ctx, ModVipAction::AddEditor);
}

QString removeEditor(const CommandContext &ctx)
{
    return runElevatedRoleCommand(ctx, ModVipAction::RemoveEditor);
}

}  // namespace chatterino::commands
